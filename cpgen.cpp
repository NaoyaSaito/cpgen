#include "cpgen.h"


namespace cp {

// init_leg_pos: 0: right, 1: left, world coodinate(leg end link)
void cpgen::initialize(const Vector3& com, const Quat& waist_r,
             const Affine3d init_leg_pose[], const Quat i_base2leg[],
             const double endcpoff[],
             double t, double sst, double dst, double cogh, double legh) {
  // init variable setup
  swingleg = left;
  end_cp_offset[0] = endcpoff[0];  end_cp_offset[1] = endcpoff[1];
  setInitLandPose(init_leg_pose);
  base2leg[0] = i_base2leg[0];  base2leg[1] = i_base2leg[1];

  setup(t, sst, dst, cogh, legh);
  comtrack.init_setup(dt, single_sup_time, double_sup_time, cog_h, com, waist_r);
  legtrack.init_setup(dt, single_sup_time, double_sup_time, leg_h, init_pose);
  // init Capture Point
  end_cp[0] = com[0];
  end_cp[1] = com[1];
  ref_zmp = comtrack.calcRefZMP(end_cp);
  land_pos = Vector3::Zero();

  wstate = stopped;

  std::cout << "[cpgen] initialize finish" << std::endl;
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
    wstate = starting1;
    std::cout << "[cpgen] Start Walking" << std::endl;
  }
}

void cpgen::stop() {
  if (wstate == walk || wstate == step) {
    wstate = stop_next;
    std::cout << "[cpgen] Stop Walking" << std::endl;
  }
}

void cpgen::estop() {
  wstate = stopped;
  std::cout << "[cpgen] Emergency Stop" << std::endl;
}

void cpgen::setLandPos(const Vector3& pose) {
  land_pos = pose;
}

void cpgen::changeSpeed(double scale) {
  single_sup_time *= scale;
  double_sup_time *= scale;
  comtrack.setup(dt, single_sup_time, double_sup_time, cog_h);
  legtrack.setup(dt, single_sup_time, double_sup_time, leg_h);
}

void cpgen::getInitWalkingPattern(Vector3* com_pos, Quat* waist_r,
                                  Pose* right_leg_pose, Pose* left_leg_pose) {
  *right_leg_pose = init_pose[0];
  *left_leg_pose  = init_pose[1];
  comtrack.calcRefZMP(end_cp);
  *com_pos = comtrack.getCoMTrack(end_cp, 0.0);
  *waist_r = comtrack.getWaistTrack(0.0);
}

void cpgen::getWalkingPattern(Vector3* com_pos, Quat* waist_r,
                              Pose* right_leg_pose, Pose* left_leg_pose) {
  static double step_delta_time = double_sup_time + single_sup_time + 1.0;

  if (wstate == stopped) return;

  // if finished a step, calc leg track and reference ZMP.
  if (step_delta_time >= double_sup_time + single_sup_time) {
    swingleg = swingleg == right ? left : right;
    calcEndCP();
    ref_zmp = comtrack.calcRefZMP(end_cp);

    // std::cout << "[cpgen] land right p: " << land_pose_w[right].p() << std::endl;
    // std::cout << "[cpgen] land left  p: " << land_pose_w[left].p() << std::endl;

    legtrack.setStepVar(land_pose_w, swingleg, wstate);
    step_delta_time = 0.0;
  }

  // push walking pattern
  static Pose leg_pose[2];
  *com_pos = comtrack.getCoMTrack(end_cp, step_delta_time);
  *waist_r = comtrack.getWaistTrack(step_delta_time);
  legtrack.getLegTrack(step_delta_time, leg_pose);
  *right_leg_pose = leg_pose[0];
  *left_leg_pose  = leg_pose[1];

  // setting flag and time if finished a step
  step_delta_time += dt;
  if (step_delta_time >= double_sup_time + single_sup_time) {
    if (wstate == starting1) {
      wstate = starting2;
    } else if (wstate == starting2) {
      if (whichwalk == step) {
        wstate = step;
      } else {
        wstate = walk;
      }
    } else if (wstate == stop_next) {
      wstate = stopping1;
    } else if (wstate == stopping1) {
      wstate = stopping2;
    } else if (wstate == stopping2) {
      wstate = stopped;
      std::cout << "[cpgen] Stopped" << std::endl;
    } else if (wstate == walk2step) {
      wstate = step;
    } else if (wstate == step2walk) {
      wstate = walk;
    }
  }
}

void cpgen::calcEndCP() {
  calcLandPos();
  if (wstate == stopping1 || wstate == stopping2 || wstate == stop_next) {
    end_cp[0] = land_pose_w[swingleg].p().x() + end_cp_offset[0];
    end_cp[1] = (land_pose_w[0].p().y() + land_pose_w[1].p().y()) * 0.5;
  } else {
    end_cp[0] = land_pose_w[swingleg].p().x() + end_cp_offset[0];
    if (swingleg == right) {
      end_cp[1] = land_pose_w[swingleg].p().y() + end_cp_offset[1];
    } else {
      end_cp[1] = land_pose_w[swingleg].p().y() - end_cp_offset[1];
    }
  }
}

// neccesary call this if change "land_pos"
void cpgen::calcLandPos() {
  whichWalkOrStep();
  static Vector3 before_land_pos(land_pos.x(), land_pos.y(), 0.0);
  static Vector2 before_land_dis(land_pos.x(), land_pos.y());

  // calc next step land position
  Vector2 next_land_distance(0.0, 0.0);
  if (wstate == starting1 || wstate == starting2 || wstate == step2walk) {
    next_land_distance[0] = land_pos.x();
    next_land_distance[1] = isCollisionLegs(land_pos.y()) ? land_pos.y() : 0.0;
  } else if (wstate == stop_next) {  // walking stop
    next_land_distance[0] = land_pos.x();
    next_land_distance[1] =
        isCollisionLegs(before_land_pos.y()) ? before_land_pos.y() : 0.0;
  } else if (wstate == walk2step) {  // walk -> step
    next_land_distance[0] = before_land_pos.x();
    next_land_distance[1] =
        isCollisionLegs(before_land_pos.y()) ? 0.0 : before_land_pos.y();
  } else if (wstate == walk) {
    next_land_distance[0] =
        before_land_pos.x() * 2 + (land_pos.x() - before_land_pos.x());
    next_land_distance[1] = isCollisionLegs(land_pos.y(), before_land_dis.y());
  } else {
    next_land_distance[0] = 0.0;
    next_land_distance[1] = 0.0;
  }

  // calc next step land rotation
  Quaternion next_swing_q, next_sup_q;
  if (land_pos.z() > 0.0) {
    if (swingleg == right) {
      next_swing_q = rpy2q(0, 0, -land_pos.z() / 2);
      next_sup_q = rpy2q(0, 0, -land_pos.z() / 2);
    } else {
      next_swing_q = rpy2q(0, 0, before_land_pos.z() / 2);
      next_sup_q = rpy2q(0, 0, before_land_pos.z() / 2);
    }
  } else {
    if (swingleg == left) {
      next_swing_q = rpy2q(0, 0, land_pos.z() / 2);
      next_sup_q = rpy2q(0, 0, land_pos.z() / 2);
    } else {
      next_swing_q = rpy2q(0, 0, -before_land_pos.z() / 2);
      next_sup_q = rpy2q(0, 0, -before_land_pos.z() / 2);
    }
  }

  // set next landing position
  // swing leg
  Vector3 swing_p(land_pose_w[swingleg].p().x() + next_land_distance[0],
                  land_pose_w[swingleg].p().y() + next_land_distance[1],
                  init_pose[swingleg].p().z());
  Quaternion swing_q = land_pose_w[swingleg].q() * next_swing_q;
  // support leg
  rl supleg = swingleg == right ? left : right;
  Vector3 sup_p(land_pose_w[supleg].p().x(), land_pose_w[supleg].p().y(),
                init_pose[supleg].p().z());
  Quaternion sup_q = land_pose_w[supleg].q() * next_sup_q;  // union angle

  // set next landing position
  land_pose_w[swingleg].set(swing_p, swing_q);
  land_pose_w[supleg].set(sup_p, sup_q);

  before_land_pos = land_pos;
  before_land_dis = next_land_distance;
}

void cpgen::whichWalkOrStep() {
  if (wstate != walk && wstate != step) {return;}
  if (land_pos.x() != 0.0 || land_pos.y() != 0.0) {
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

bool cpgen::isCollisionLegs(double y) {
  if ((swingleg == right && y < 0) || (swingleg == left && y >= 0)) {
    return true;
  } else {
    return false;
  }
}

double cpgen::isCollisionLegs(double yn, double yb) {
  if (swingleg == right && yn < 0) {
    if (yb > 0) return 0.0;
    else return yn;
  } else if (swingleg == left && yn >= 0) {
    if (yb < 0) return 0.0;
    else return yn;
  } else {
    return yb;
  }
}

// initialze "land_pose_w"
void cpgen::setInitLandPose(const Affine3d init_leg_pose[]) {
  for (int i = 0; i < 2; ++i) {
    Vector3 trans = init_leg_pose[i].translation();
    Quaternion q = Quaternion(init_leg_pose[i].rotation());

    init_pose[i].set(trans, q);
    land_pose_w[i].set(init_pose[i]);
  }
  feet_dist = fabs(land_pose_w[0].p().y()) + fabs(land_pose_w[1].p().y());
}

}  // namespace cp
