#include "cpgen.h"

namespace cp {

// init_leg_pos: 0: right, 1: left, world coodinate(leg end link)
cpgen::cpgen(const Vector3& com, const std::array<Affine3d, 2>& init_leg_pos,
             const std::array<cp::Quaternion, 2>& i_base2leg,
             double t, double sst, double dst, double cogh, double legh) {
  // init variable setup
  swingleg = left;
  dt = 5e-3;
  single_sup_time = 0.5;
  double_sup_time = 0.2;
  cog_h = com[2];
  leg_h = 0.02;
  end_cp_offset[0] = 0.001;
  end_cp_offset[1] = 0.02;
  setInitLandPos(init_leg_pos);
  base2leg = i_base2leg;

  setup(t, sst, dst, cogh, legh);
  comtrack.init_setup(dt, single_sup_time, double_sup_time, cog_h, com);
  legtrack.init_setup(dt, single_sup_time, double_sup_time, leg_h,
                      land_pos_leg_w);
  // init Capture Point
  end_cp[0] = com[0];
  end_cp[1] = com[1];

  land_pos = Vector3::Zero();

  wstate = stopped;

  std::cout << "[cpgen] activate" << std::endl;
}

// always can change these value
// @param t: sampling time
// @param sst: single support time
// @param dst: double support time
// @param cogh: height center of gravity
// @param legh: height of up leg
void cpgen::setup(double t, double sst, double dst, double cogh, double legh) {
  dt = t;
  single_sup_time = sst;
  double_sup_time = dst;
  cog_h = cogh;
  leg_h = legh;

  comtrack.setup(dt, single_sup_time, double_sup_time, cog_h);
  legtrack.setup(dt, single_sup_time, double_sup_time, leg_h);
}

void cpgen::start() {
  if (wstate == stopped) {
    wstate = starting;
    std::cout << "[cpgen] Start Walking" << std::endl;
  }
}

void cpgen::stop() {
  if (wstate == walk || wstate == step) {
    wstate = stop_next;
    std::cout << "[cpgen] stopped Walking" << std::endl;
  }
}

void cpgen::restart() {
  wstate = starting;
  std::cout << "[cpgen] Restart Walking" << std::endl;
}

void cpgen::setLandPos(const Vector3& pose) { land_pos = pose; }

void cpgen::getWalkingPattern(const Vector3& com, Vector3* com_pos,
                              Pose* right_leg_pos, Pose* left_leg_pos) {
  static double step_delta_time = 0.0;

  if (wstate == stopped) {
    return;
  }

  // if finished a step, calc leg track and reference ZMP.
  if (designed_leg_track[0].empty()) {
    swingleg = swingleg == right ? left : right;
    calcEndCP();
    ref_zmp = comtrack.calcRefZMP(end_cp);

    legtrack.getLegTrack(swingleg, land_pos_leg_w, designed_leg_track);
    step_delta_time = 0.0;
  }

  // push walking pattern
  *com_pos = comtrack.getCoMTrack(com, end_cp, step_delta_time);
  *right_leg_pos = designed_leg_track[right].front();
  *left_leg_pos = designed_leg_track[left].front();
  designed_leg_track[right].pop_front();
  designed_leg_track[left].pop_front();

  // setting flag and time if finished a step
  if (designed_leg_track[0].empty()) {
    if (wstate == starting) {
      if (whichwalk == step) {
        wstate = step;
      } else {
        wstate = walk;
      }
    } else if (wstate == stopping) {
      wstate = stopped;
    } else if (wstate == walk2step) {
      wstate = step;
    } else if (wstate == step2walk) {
      wstate = walk;
    }
  }
  step_delta_time += dt;
}

void cpgen::calcEndCP() {
  calcLandPos();
  if (wstate == stopping) {
    end_cp[0] = land_pos_leg_w[swingleg].p().x();
    end_cp[1] = (land_pos_leg_w[0].p().y() + land_pos_leg_w[1].p().y()) / 2;
  } else {
    end_cp[0] = land_pos_leg_w[swingleg].p().x() + end_cp_offset[0];
    if (swingleg == right) {
      end_cp[1] = land_pos_leg_w[swingleg].p().y() + end_cp_offset[1];
    } else {
      end_cp[1] = land_pos_leg_w[swingleg].p().y() - end_cp_offset[1];
    }
  }
}

// neccesary call this if change "land_pos"
void cpgen::calcLandPos() {
  whichWalkOrStep();
  static Vector2 before_land_pos(land_pos.x(), land_pos.y());

  // calc next step land position
  Vector2 next_land_distance(0.0, 0.0);
  if (wstate == starting || wstate == step2walk) {  // walking start
    next_land_distance[0] = land_pos.x();
    // prevent collision both leg
    if ((swingleg == right && land_pos.y() > 0) ||
        (swingleg == left && land_pos.y() < 0)) {
      next_land_distance[1] = 0.0;
    } else {
      next_land_distance[1] = land_pos.y();
    }
  } else if (wstate == stop_next) {  // walking stop
    next_land_distance[0] = land_pos.x();
    // for arrange both leg
    if (((swingleg == right && before_land_pos.y() > 0) ||
         (swingleg == left && before_land_pos.y() < 0))) {
      next_land_distance[1] = before_land_pos.y();
    } else {
      next_land_distance[1] = 0.0;
    }
    wstate = stopping;
  } else if (wstate == walk2step) {
    next_land_distance[0] = before_land_pos.x();
    // for arrange both leg
    if ((swingleg == right && before_land_pos.y() > 0) ||
        (swingleg == left && before_land_pos.y() < 0)) {
      next_land_distance[1] = before_land_pos.y();
    } else {
      next_land_distance[1] = 0.0;
    }
  } else if (wstate == walk) {
    // correspond accelate
    next_land_distance[0] =
        before_land_pos.x() * 2 + (land_pos.x() - before_land_pos.x());
    // TODO! correspond Y asix accelation chagen of minus
    if (((swingleg == right && (land_pos.y() - before_land_pos.y()) > 0) ||
         (swingleg == left && (land_pos.y() - before_land_pos.y()) < 0))) {
      next_land_distance[1] = before_land_pos.y();
    } else {
      next_land_distance[1] = land_pos.y();
    }
  } else {
    next_land_distance[0] = 0.0;
    next_land_distance[1] = 0.0;
  }
  std::cout << wstate << ", " << swingleg << ": " << next_land_distance[0]
            << ", " << next_land_distance[1] << std::endl;

  // calc next step land rotation
  Quaternion next_swing_q, next_sup_q;
  if (swingleg == right) {
    next_swing_q = rpy2q(0, 0, land_pos.z());
    next_sup_q = rpy2q(0, 0, land_pos.z());
  } else {
    next_swing_q = rpy2q(0, 0, -land_pos.z());
    next_sup_q = rpy2q(0, 0, -land_pos.z());
  }

  // set next landing position
  // swing leg
  Vector3 swing_p(land_pos_leg_w[swingleg].p().x() + next_land_distance[0],
                  land_pos_leg_w[swingleg].p().y() + next_land_distance[1],
                  0.0);
  Quaternion swing_q = land_pos_leg_w[swingleg].q() * next_swing_q;
  // support leg
  rl supleg = swingleg == right ? left : right;
  Vector3 sup_p(land_pos_leg_w[supleg].p().x(), land_pos_leg_w[supleg].p().y(),
                0.0);
  Quaternion sup_q = land_pos_leg_w[supleg].q() * next_sup_q;  // union angle

  // set next landing position
  land_pos_leg_w[swingleg].set(swing_p, swing_q);
  land_pos_leg_w[supleg].set(sup_p, sup_q);

  before_land_pos[0] = land_pos.x(); // TODO!
  before_land_pos[1] = land_pos.y();
}

void cpgen::whichWalkOrStep() {
  if (land_pos.x() != 0.0 || land_pos.y() != 0.0 || land_pos.z() != 0.0) {
    if (whichwalk == step) {
      wstate = step2walk;
    }
    whichwalk = walk;
  } else {
    if (whichwalk == walk) {
      wstate = walk2step;
    }
    whichwalk = step;
  }
}

bool cpgen::isYSwingCorrect(double y) {
  if ((swingleg == right && y > 0) || (swingleg == left && y < 0)) {
    return false;
  } else {
    return true;
  }
}

// initialze "land_pos_leg_w"
void cpgen::setInitLandPos(const std::array<Affine3d, 2>& init_leg_pos) {
  for (int i = 0; i < 2; ++i) {
    Vector3 trans = init_leg_pos[i].translation();
    Quaternion q = Quaternion(init_leg_pos[i].rotation());

    land_pos_leg_w[i].set(trans, q);
  }
}

Pose cpgen::setInitLandPos(const Affine3d& init_leg_pos) {
  Vector3 trans = init_leg_pos.translation();
  Quaternion q = Quaternion(init_leg_pos.rotation());
  cp::Pose pose;
  pose.set(trans, q);
  return pose;
}

}  // namespace cp