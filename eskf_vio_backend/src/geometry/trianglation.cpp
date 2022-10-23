/* 内部依赖 */
#include <sequence_propagator.hpp>
#include <math_lib.hpp>
#include <log_api.hpp>
#include<trianglation.hpp>
/* 外部依赖 */
/*
      translation_threshold(0.2),
      huber_epsilon(0.01),
      estimation_precision(5e-7),
      initial_damping(1e-3),
      outer_loop_max_iteration(10),
      inner_loop_max_iteration(10) {

*/
# define INNER_LOOP_MAX_ITER (10) //inner_loop_max_iteration
# define OUTER_LOOP_MAX_ITER (10)//outer_loop_max_iteration
# define ESTIMATION_PRECISION (5e-7)//estimation_precision
namespace ESKF_VIO_BACKEND {

bool Trianglator::TrianglateAnalytic(const std::vector<Quaternion> &q_wc,
                                const std::vector<Vector3> &p_wc,
                                const std::vector<Vector2> &norm,
                                Vector3 &p_w)
{
    int observe_num = q_wc.size();
    // Eigen::Matrix<Scalar,observe_num *2,4> design_matrix = Eigen::Matrix<Scalar,obseve_num *2,4>::Zero();
    Matrix design_matrix;
    design_matrix.resize(observe_num * 2, 4);
    for (int i =0; i < observe_num; i++)
    {
        auto Pose =  Utility::qtToTransformMatrix(q_wc[i], p_wc[i]);
        design_matrix.row(2*i) = norm[i][0] * Pose.row(2) - Pose.row(0);
        design_matrix.row(2*i+1) = norm[i][1] * Pose.row(2) - Pose.row(1);
    }
    Eigen::Matrix<Scalar,4,1> triangulated_point;
    triangulated_point =
              design_matrix.jacobiSvd(Eigen::ComputeFullV).matrixV().rightCols<1>();//TODO: discuss if thin or full
    // 齐次向量归一化
    p_w(0) = triangulated_point(0) / triangulated_point(3);
    p_w(1) = triangulated_point(1) / triangulated_point(3);
    p_w(2) = triangulated_point(2) / triangulated_point(3);
    return true;
}

void Trianglator::jacobian(const Quaternion& R_c0_ci, const Vector3& t_c0_ci,
    const Vector3& x, const Vector2& z, Eigen::Matrix<Scalar, 2, 3>& J, Vector2& r,
    Scalar& w) {

    Scalar huber_epsilon(0.01); 
    // Compute hi1, hi2, and hi3 as Equation (37).
    const Scalar& alpha = x(0);
    const Scalar& beta = x(1);
    const Scalar& rho = x(2);

    Vector3 h = R_c0_ci.toRotationMatrix() *
      Vector3(alpha, beta, 1.0) + rho*t_c0_ci;
    Scalar& h1 = h(0);
    Scalar& h2 = h(1);
    Scalar& h3 = h(2);

    // Compute the Jacobian.
    Matrix33 W;
    W.leftCols<2>() = R_c0_ci.toRotationMatrix().leftCols<2>();
    W.rightCols<1>() = t_c0_ci;

    J.row(0) = 1/h3*W.row(0) - h1/(h3*h3)*W.row(2);
    J.row(1) = 1/h3*W.row(1) - h2/(h3*h3)*W.row(2);

    // Compute the residual.
    Vector2 z_hat(h1/h3, h2/h3);
    r = z_hat - z;

    // Compute the weight based on the residual.
    Scalar e = r.norm();
    if (e <= huber_epsilon)
      w = 1.0;
    else
      w = std::sqrt(2.0* huber_epsilon / e);

    return;
}


bool Trianglator::TrianglateIterative(const std::vector<Quaternion> &q_wc,
                            const std::vector<Vector3> &p_wc,
                            const std::vector<Vector2> &norm,
                            Vector3 &p_w)
  {
    Vector3 solution(
    p_w(0)/p_w(2),
    p_w(1)/p_w(2),
    1.0/p_w(2));

    int total_poses = q_wc.size();

    // Apply Levenberg-Marquart method to solve for the 3d position.
    double lambda =1e-3/*initial_damping*/;
    int inner_loop_cntr = 0;
    int outer_loop_cntr = 0;
    bool is_cost_reduced = false;
    double delta_norm = 0;

    // Compute the initial cost.
    Scalar total_cost = 0.0;
    for (int i = 0; i < total_poses; ++i) {
      total_cost += getReprojectionCost(q_wc[i], p_wc[i], p_w, norm[i]);
    }
    // LogDebug("init cost "<<total_cost);

    // Outer loop.
    do {
      Matrix33 A = Matrix33::Zero();
      Vector3 b = Vector3::Zero();

      for (int i = 0; i < total_poses; ++i) {
        Eigen::Matrix<Scalar, 2, 3> J;
        Vector2 r;
        Scalar w;

        jacobian(q_wc[i], p_wc[i], solution, norm[i], J, r, w);

        if (w == 1) {
          A += J.transpose() * J;
          b += J.transpose() * r;
        } else {
          double w_square = w * w;
          A += w_square * J.transpose() * J;
          b += w_square * J.transpose() * r;
        }
      }

      // Inner loop.
      // Solve for the delta that can reduce the total cost.
      do {
        Matrix33 damper = lambda * Matrix33::Identity();
        Vector3 delta = (A+damper).ldlt().solve(b);
        Vector3 new_solution = solution - delta;
          p_w = Vector3(new_solution(0)/new_solution(2),
      new_solution(1)/new_solution(2), 1.0/new_solution(2));
        delta_norm = delta.norm();

        double new_cost = 0.0;
        for (int i = 0; i < total_poses; ++i) {
          new_cost += getReprojectionCost(q_wc[i], p_wc[i], p_w, norm[i]);
        }
        // LogDebug("New cost: "<<new_cost);

        if (new_cost < total_cost) {
          is_cost_reduced = true;
          solution = new_solution;
          total_cost = new_cost;
          lambda = lambda/10 > 1e-10 ? lambda/10 : 1e-10;
        } else {
          is_cost_reduced = false;
          p_w = Vector3(solution(0)/solution(2),solution(1)/solution(2), 1.0/solution(2));
          lambda = lambda*10 < 1e12 ? lambda*10 : 1e12;
        }

    } while (inner_loop_cntr++ <
        INNER_LOOP_MAX_ITER && !is_cost_reduced);

    inner_loop_cntr = 0;

  } while (outer_loop_cntr++ <
      OUTER_LOOP_MAX_ITER &&
      delta_norm > ESTIMATION_PRECISION);

  // Covert the feature position from inverse depth
  // representation to its 3d coordinate.
  p_w = Vector3(solution(0)/solution(2),
      solution(1)/solution(2), 1.0/solution(2));
// TODO: when to return false?
  return true;
}

/*measure the accuracy of the reprojection estimation*/
Scalar Trianglator::getReprojectionCost(const Quaternion& q, const Vector3& t, const Vector3& lm, const Vector2& groundtruth ) { 
  // Compute hi1, hi2, and hi3 as Equation (37).
  const Scalar& alpha = lm(0)/lm(2);
  const Scalar& beta = lm(1)/lm(2);
  const Scalar& rho = 1.0/lm(2);

  Vector3 h = q.toRotationMatrix()* Vector3(alpha, beta, 1.0) + rho*t;
  Scalar& h1 = h(0);
  Scalar& h2 = h(1);
  Scalar& h3 = h(2);

  // Predict the feature observation in ci frame.
  Vector2 z_hat(h1/h3, h2/h3);

  // Compute the residual.
  return (z_hat-groundtruth).squaredNorm();
}

}