#ifndef THRUSTER_CONTROLLER_H
#define THRUSTER_CONTROLLER_H

#include <math.h>

#include "ceres/ceres.h"
#include "glog/logging.h"

#include "ros/ros.h"
#include "tf/transform_listener.h"
#include <dynamic_reconfigure/server.h>
#include <riptide_controllers/VehiclePropertiesConfig.h>

#include "geometry_msgs/Vector3.h"
#include "geometry_msgs/Vector3Stamped.h"
#include "geometry_msgs/Accel.h"
#include "riptide_msgs/Imu.h"
#include "imu_3dm_gx4/FilterOutput.h"
#include "riptide_msgs/Depth.h"
#include "riptide_msgs/ThrustStamped.h"
#include "riptide_msgs/ThrusterResiduals.h"

#include <yaml-cpp/yaml.h>
#include "eigen3/Eigen/Dense"
#include "eigen3/Eigen/Core"
using namespace Eigen;
//#include "sstream"

#define PI 3.141592653
#define GRAVITY 9.81         //[m/s^2]
#define WATER_DENSITY 1000.0 //[kg/m^3]

typedef Matrix<double, 6, 1> Vector6d;
typedef Matrix<double, 6, 8> Matrix68d;
typedef Matrix<double, Dynamic, Dynamic, RowMajor> RowMatrixXd;

class ThrusterController
{
private:
  // Comms
  ros::NodeHandle nh;
  ros::Subscriber state_sub, cmd_sub, depth_sub, mass_vol_sub, buoyancy_sub;
  ros::Publisher cmd_pub, buoyancy_pub, residual_pub;
  riptide_msgs::ThrustStamped thrust;
  riptide_msgs::ThrusterResiduals residuals;

  bool debug_controller; // If true, key params can be input via messages
  dynamic_reconfigure::Server<riptide_controllers::VehiclePropertiesConfig> server;
  dynamic_reconfigure::Server<riptide_controllers::VehiclePropertiesConfig>::CallbackType cb;

  // Variables for New Thruster Setup
  ros::Publisher new_pub;
  riptide_msgs::ThrustStamped thrust2;
  YAML::Node VProperties; // Vehicle Properties
  std::vector<int> thrustersEnabled;
  Vector3d CoB;
  double M, V, W, B, Jxx, Jyy, Jzz;
  
  // Primary EOMs
  ceres::Problem problem;
  ceres::Solver::Options options;
  ceres::Solver::Summary summary;

  // Locate Buoyancy EOMs
  ceres::Problem buoyancyProblem;
  ceres::Solver::Options buoyancyOptions;
  ceres::Solver::Summary buoyancySummary;

  // New EOM Format
  ceres::Problem newProblem;
  ceres::Solver::Options newOptions;
  ceres::Solver::Summary newSummary;

  MatrixXd thrustFM_eig, thrusters;
  Vector6d weightFM_eig;
  double forces[8]; // Solved forces go here

public:
  // These variables need to be public so class EOM can use them
  /*int numThrusters;
  Matrix68d thrustFM_eig;
  Vector6d inertia, weightFM_eig, transportThm_eig, command;*/
  int numThrusters;
  double thrustFM[6][8], inertia[6], weightFM[6], transportThm[6], command[6];
  
  EIGEN_MAKE_ALIGNED_OPERATOR_NEW

  ThrusterController(char **argv);
  template <typename T>
  void LoadParam(std::string param, T &var);
  void LoadVehicleProperties();
  void SetupThrusters();
  void InitThrustMsg();
  void DynamicReconfigCallback(riptide_controllers::VehiclePropertiesConfig &config, uint32_t levels);
  void ImuCB(const riptide_msgs::Imu::ConstPtr &imu_msg);
  void DepthCB(const riptide_msgs::Depth::ConstPtr &depth_msg);
  void AccelCB(const geometry_msgs::Accel::ConstPtr &a);
  void Loop();
};

class EOM
{
  private:
  /*int* numThrusters;
  double* thrustFM[8];
  double *inertia, *weightFM, *transportThm, *command;*/
  ThrusterController* tc;
  
  public:
  EOM(ThrusterController* tcIn)
  {
    tc = tcIn;
  }
  /*EOM(int* num, double** thrustFMIn, double* inertiaIn, double* weightFMIn, double* transportThmIn, double* commandIn)
  {
    numThrusters = num;
    inertia = inertiaIn;
    weightFM = weightFMIn;
    transportThm = transportThmIn;
    command = commandIn;

    for(int i = 0; i < 6; i++)
      thrustFM[i] = (double *)thrustFMIn;
  }*/
  
  template <typename T>
  bool operator()(const T *const forces, T *residual) const
  {
    for (int i = 0; i < 6; i++)
    {
      residual[i] = T(0);

      // Account for each thruster's contribution
      for (int j = 0; j < tc->numThrusters; j++)
      {
        residual[i] = residual[i] + T(tc->thrustFM[i][j]) * forces[j];
      }

      // Account for weightFM and transportThm
      residual[i] = residual[i] + T(tc->weightFM[i] + tc->transportThm[i]);
      residual[i] = residual[i] / T(tc->inertia[i]) - T(tc->command[i]);
    }
    return true;
  }
};

//***** Below are all the variables that are needed for ceres *****///////////////////////////////
// **Please keep these in case they get deleted from the vehicle_properties.yaml file
/*#define Ixx 0.52607145
#define Iyy 1.50451601
#define Izz 1.62450600*/
double Ixx, Iyy, Izz;

struct vector
{
  double x;
  double y;
  double z;
};

// Thrust limits (N):
// These limits cannot be set too low b/c otherwise it will interfere with
// the EOMs and result in additional thrusters turning on to maintain those
// relationships. Ex. surge and sway will kick in and move the vehicle at a diagonal
// when the heave thrust is capped at too low of a number. If these limits are
// laxed, then the solver will NOT turn on those additional thrusters and the
// output will be as expected.
// NOTE: For the time being, the upper/lower bounds have been REMOVED from the solver
double MIN_THRUST = -24.0;
double MAX_THRUST = 24.0;

// Vehicle mass (kg):
// Updated 5-15-18
double mass;
double weight = mass * GRAVITY;

// Vehcile volume (m^3)
// TODO: Get this value from model
// Updated on 5/11/18
double volume;
double buoyancy = volume * WATER_DENSITY * GRAVITY;

/*// Moments of inertia (kg*m^2)
double Ixx = 0.52607145;
double Iyy = 1.50451601;
double Izz = 1.62450600;*/

// Acceleration commands (m/s^):
double cmdSurge = 0.0;
double cmdSway = 0.0;
double cmdHeave = 0.0;
double cmdRoll = 0.0;
double cmdPitch = 0.0;
double cmdYaw = 0.0;

// Solved Thruster Forces
double surge_port_lo, surge_stbd_lo;
double sway_fwd, sway_aft;
double heave_port_aft, heave_stbd_aft, heave_stbd_fwd, heave_port_fwd;

// Thruster Status
bool enableSPL, enableSSL, enableSWF, enableSWA, enableHPF, enableHSF, enableHPA, enableHSA;

// Buoyancy Variables
bool isBuoyant;
double pos_buoyancy_x, pos_buoyancy_y, pos_buoyancy_z;
double buoyancy_depth_thresh;

// Rotation Matrices: world to body, and body to world
// Angular Velocity
tf::Matrix3x3 R_w2b, R_b2w;
tf::Vector3 euler_deg, euler_rpy, ang_v;

// Debug variables
geometry_msgs::Vector3Stamped buoyancy_pos;

/*** Thruster Positions ***/
// Positions are in meters relative to the center of mass (can be neg. or pos.)
vector pos_surge_port_lo;
vector pos_surge_stbd_lo;
vector pos_sway_fwd;
vector pos_sway_aft;
vector pos_heave_port_fwd;
vector pos_heave_port_aft;
vector pos_heave_stbd_fwd;
vector pos_heave_stbd_aft;

// Buoyancy Location
vector pos_buoyancy;

/*** EQUATIONS ***/ ////////////////////////////////////////////////////////////
// These equations solve for linear/angular acceleration in all axes

// Linear Equations
struct surge
{
  template <typename T>
  bool operator()(const T *const surge_port_lo, const T *const surge_stbd_lo, T *residual) const
  {
    residual[0] = (surge_port_lo[0] + surge_stbd_lo[0] +
                   (T(R_w2b.getRow(0).z()) * (T(buoyancy) - T(weight)) * T(isBuoyant))) /
                      T(mass) -
                  T(cmdSurge);
    return true;
  }
};

struct sway
{
  template <typename T>
  bool operator()(const T *const sway_fwd, const T *const sway_aft, T *residual) const
  {
    residual[0] = (sway_fwd[0] + sway_aft[0] +
                   (T(R_w2b.getRow(1).z()) * (T(buoyancy) - T(weight)) * T(isBuoyant))) /
                      T(mass) -
                  T(cmdSway);
    return true;
  }
};

struct heave
{
  template <typename T>
  bool operator()(const T *const heave_port_fwd, const T *const heave_stbd_fwd,
                  const T *const heave_port_aft, const T *const heave_stbd_aft, T *residual) const
  {

    residual[0] = (heave_port_fwd[0] + heave_stbd_fwd[0] + heave_port_aft[0] + heave_stbd_aft[0] +
                   (T(R_w2b.getRow(2).z()) * (T(buoyancy) - T(weight)) * T(isBuoyant))) /
                      T(mass) -
                  T(cmdHeave);
    return true;
  }
};

// Angular equations

// Roll
// Thrusters contributing to a POSITIVE moment: sway_fwd, sway_aft, heave_port_fwd, heave_port_aft
// Thrusters contributting to a NEGATIVE moment: heave_stbd_fwd, heave_stbd_aft
// Buoyancy Y and Z components produce moments about x-axis
struct roll
{
  template <typename T>
  bool operator()(const T *const sway_fwd, const T *const sway_aft,
                  const T *const heave_port_fwd, const T *const heave_stbd_fwd,
                  const T *const heave_port_aft, const T *const heave_stbd_aft, T *residual) const
  {
    residual[0] = ((T(R_w2b.getRow(1).z()) * T(buoyancy) * T(-pos_buoyancy.z) +
                    T(R_w2b.getRow(2).z()) * T(buoyancy) * T(pos_buoyancy.y)) *
                       T(isBuoyant) +
                   sway_fwd[0] * T(-pos_sway_fwd.z) + sway_aft[0] * T(-pos_sway_aft.z) +
                   heave_port_fwd[0] * T(pos_heave_port_fwd.y) + heave_stbd_fwd[0] * T(pos_heave_stbd_fwd.y) +
                   heave_port_aft[0] * T(pos_heave_port_aft.y) + heave_stbd_aft[0] * T(pos_heave_stbd_aft.y) -
                   ((T(ang_v.z()) * T(ang_v.y())) * (T(Izz) - T(Iyy)))) /
                      T(Ixx) -
                  T(cmdRoll);
    return true;
  }
};

// Pitch
// Thrusters contributing to a POSITIVE moment: heave_port_aft, heave_stbd_aft
// Thrusters contributting to a NEGATIVE moment: surge_port_lo, surge_stbd_lo, heave_port_fwd, heave_stbd_fwd
// Buoyancy X and Z components produce moments about y-axis
struct pitch
{
  template <typename T>
  bool operator()(const T *const surge_port_lo, const T *const surge_stbd_lo,
                  const T *const heave_port_fwd, const T *const heave_stbd_fwd,
                  const T *const heave_port_aft, const T *const heave_stbd_aft, T *residual) const
  {
    residual[0] = ((T(R_w2b.getRow(0).z()) * T(buoyancy) * T(pos_buoyancy.z) +
                    T(R_w2b.getRow(2).z()) * T(buoyancy) * T(-pos_buoyancy.x)) *
                       T(isBuoyant) +
                   surge_port_lo[0] * T(pos_surge_port_lo.z) + surge_stbd_lo[0] * T(pos_surge_stbd_lo.z) +
                   heave_port_fwd[0] * T(-pos_heave_port_fwd.x) + heave_stbd_fwd[0] * T(-pos_heave_stbd_fwd.x) +
                   heave_port_aft[0] * T(-pos_heave_port_aft.x) + heave_stbd_aft[0] * T(-pos_heave_stbd_aft.x) -
                   ((T(ang_v.x()) * T(ang_v.z())) * (T(Ixx) - T(Izz)))) /
                      T(Iyy) -
                  T(cmdPitch);
    return true;
  }
};

// Yaw
// Thrusters contributing to a POSITIVE moment: surge_stbd_lo, sway_fwd
// Thrusters contributting to a NEGATIVE moment: surge_port_lo, sway_aft
// Buoyancy X and Y components produce moments about z-axis
struct yaw
{
  template <typename T>
  bool operator()(const T *const surge_port_lo, const T *const surge_stbd_lo,
                  const T *const sway_fwd, const T *const sway_aft, T *residual) const
  {
    residual[0] = ((T(R_w2b.getRow(0).z()) * T(buoyancy) * T(-pos_buoyancy.y) +
                    T(R_w2b.getRow(1).z()) * T(buoyancy) * T(pos_buoyancy.x)) *
                       T(isBuoyant) +
                   surge_port_lo[0] * T(-pos_surge_port_lo.y) + surge_stbd_lo[0] * T(-pos_surge_stbd_lo.y) +
                   sway_fwd[0] * T(pos_sway_fwd.x) + sway_aft[0] * T(pos_sway_aft.x) -
                   ((T(ang_v.y()) * T(ang_v.x())) * (T(Iyy) - T(Ixx)))) /
                      T(Izz) -
                  T(cmdYaw);
    return true;
  }
};

// NOTE: It seems that ceres already tries to minimze all outputs as it solves.
// Hence, it seems unnecessary to add two more equations to create a
// SLE (system of linear eqns) composed of 8 equations and 8 unknowns)

/******************************* Tune Buoyancy ********************************/
// Purpose: Find the Center of Buoyancy (CoB)
// These equations ASSUME the vehicle is stationary in the water, attempting to
// reach a target orientation, but is unable to reach the said target because
// the moments due to buoyancy have not been factored into the angular eqns yet.
// The publised output will be the location of the CoB in relation to the CoM
// NOTE: Vehicle MUST be roughly stationary for output to make physical sense

// Tune Roll
// Thrusters contributing to a POSITIVE moment: sway_fwd, sway_aft, heave_port_fwd, heave_port_aft
// Thrusters contributting to a NEGATIVE moment: heave_stbd_fwd, heave_stbd_aft
// Buoyancy Y and Z components produce moments about x-axis
struct tuneRoll
{
  template <typename T>
  bool operator()(const T *const pos_buoyancy_y, const T *const pos_buoyancy_z, T *residual) const
  {
    residual[0] = T(R_w2b.getRow(1).z()) * T(buoyancy) * (-pos_buoyancy_z[0]) +
                  T(R_w2b.getRow(2).z()) * T(buoyancy) * pos_buoyancy_y[0] +
                  T(sway_fwd) * T(-pos_sway_fwd.z) + T(sway_aft) * T(-pos_sway_aft.z) +
                  T(heave_port_fwd) * T(pos_heave_port_fwd.y) + T(heave_port_aft) * T(pos_heave_port_aft.y) +
                  T(heave_stbd_fwd) * T(pos_heave_stbd_fwd.y) + T(heave_stbd_aft) * T(pos_heave_stbd_aft.y) -
                  (T(ang_v.z()) * T(ang_v.y())) * (T(Izz) - T(Iyy));
    return true;
  }
};

// Tune Pitch
// Thrusters contributing to a POSITIVE moment: heave_port_aft, heave_stbd_aft
// Thrusters contributting to a NEGATIVE moment: surge_port_lo, surge_stbd_lo, heave_port_fwd, heave_stbd_fwd
// Buoyancy X and Z components produce moments about y-axis
struct tunePitch
{
  template <typename T>
  bool operator()(const T *const pos_buoyancy_x, const T *const pos_buoyancy_z, T *residual) const
  {
    residual[0] = T(R_w2b.getRow(0).z()) * T(buoyancy) * pos_buoyancy_z[0] +
                  T(R_w2b.getRow(2).z()) * T(buoyancy) * (-pos_buoyancy_x[0]) +
                  T(surge_port_lo) * T(pos_surge_port_lo.z) + T(surge_stbd_lo) * T(pos_surge_stbd_lo.z) +
                  T(heave_port_aft) * T(-pos_heave_port_aft.x) + T(heave_stbd_aft) * T(-pos_heave_stbd_aft.x) +
                  T(heave_port_fwd) * T(-pos_heave_port_fwd.x) + T(heave_stbd_fwd) * T(-pos_heave_stbd_fwd.x) -
                  (T(ang_v.x()) * T(ang_v.z())) * (T(Ixx) - T(Izz));
    return true;
  }
};

// Tune Yaw
// Thrusters contributing to a POSITIVE moment: surge_stbd_lo, sway_fwd
// Thrusters contributting to a NEGATIVE moment: surge_port_lo, sway_aft
// Buoyancy X and Y components produce moments about z-axis
struct tuneYaw
{
  template <typename T>
  bool operator()(const T *const pos_buoyancy_x, const T *const pos_buoyancy_y, T *residual) const
  {
    residual[0] = T(R_w2b.getRow(0).z()) * T(buoyancy) * (-pos_buoyancy_y[0]) +
                  T(R_w2b.getRow(1).z()) * T(buoyancy) * (pos_buoyancy_x[0]) +
                  T(surge_port_lo) * T(-pos_surge_port_lo.y) + T(surge_stbd_lo) * T(-pos_surge_stbd_lo.y) +
                  T(sway_fwd) * T(pos_sway_fwd.x) + T(sway_aft) * T(pos_sway_aft.x) -
                  (T(ang_v.y()) * T(ang_v.x())) * (T(Iyy) - T(Ixx));
    return true;
  }
};

/************************** Reconfigure Active Thrusters **********************/
// These structs are used only if a thruster is down (problem with the copro,
// thruster itself, etc.). They will force ceres to set their thrust output to
// zero, forcing it to change how it uses the other active thrusters to provide
// the desired acceleration.

// Disable Surge Port Lo
struct disableSPL
{
  template <typename T>
  bool operator()(const T *const surge_port_lo, T *residual) const
  {
    residual[0] = surge_port_lo[0];
    return true;
  }
};

// Disable Surge Stbd Lo
struct disableSSL
{
  template <typename T>
  bool operator()(const T *const surge_stbd_lo, T *residual) const
  {
    residual[0] = surge_stbd_lo[0];
    return true;
  }
};

// Disable Sway Fwd
struct disableSWF
{
  template <typename T>
  bool operator()(const T *const sway_fwd, T *residual) const
  {
    residual[0] = sway_fwd[0];
    return true;
  }
};

// Disable Sway Aft
struct disableSWA
{
  template <typename T>
  bool operator()(const T *const sway_aft, T *residual) const
  {
    residual[0] = sway_aft[0];
    return true;
  }
};

// Disable Heave Port Fwd
struct disableHPF
{
  template <typename T>
  bool operator()(const T *const heave_port_fwd, T *residual) const
  {
    residual[0] = heave_port_fwd[0];
    return true;
  }
};

// Disable Heave Stbd Fwd
struct disableHSF
{
  template <typename T>
  bool operator()(const T *const heave_stbd_fwd, T *residual) const
  {
    residual[0] = heave_stbd_fwd[0];
    return true;
  }
};

// Disable Heave Port Aft
struct disableHPA
{
  template <typename T>
  bool operator()(const T *const heave_port_aft, T *residual) const
  {
    residual[0] = heave_port_aft[0];
    return true;
  }
};

// Disable Heave Stbd Aft
struct disableHSA
{
  template <typename T>
  bool operator()(const T *const heave_stbd_aft, T *residual) const
  {
    residual[0] = heave_stbd_aft[0];
    return true;
  }
};
///////////////////////////////////////////////////////////////////////////////

#endif
