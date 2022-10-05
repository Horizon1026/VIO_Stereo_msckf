/* 内部依赖 */
#include <backend.hpp>
/* 外部依赖 */

namespace ESKF_VIO_BACKEND {
    /* 尝试进行初始化 */
    bool Backend::Initialize(void) {
        // 在没有进行初始化时，仅在存在连续两帧观测时才进行初始化
        if (this->frameManager.frames.size() >= 2) {
            auto frame_i = this->frameManager.frames.front();
            auto frame_j = *std::next(this->frameManager.frames.begin());
            // Step 1: 从 attitude estimator 中拿取此帧对应时刻的姿态估计结果，赋值给首帧的 q_wb
            RETURN_IF_FALSE(this->attitudeEstimator.GetAttitude(frame_i->timeStamp,
                                                                frame_i->q_wb));
            // Step 2: 首帧观测为 multi-view 时，首帧位置设置为原点，进行首帧内的多目三角测量，得到一些特征点的 p_w
            frame_i->p_wb.setZero();
            RETURN_IF_FALSE(this->TrianglizeMultiView(frame_i));
            // Step 3: 利用这些特征点在下一帧中的观测，通过重投影 PnP 迭代，估计出下一帧基于首帧参考系 w 系的相对位姿 p_wb 和 q_wb
            RETURN_IF_FALSE(this->EstimateFramePose(frame_j));
            // Step 4: 两帧位置对时间进行差分，得到两帧在 w 系中的速度估计，至此首帧对应时刻点的 q_wb 和 v_wb 都已经确定
            frame_j->v_wb = (frame_j->p_wb - frame_i->p_wb) / Scalar(frame_j->timeStamp - frame_i->timeStamp);
            frame_i->v_wb = frame_j->v_wb;
            // Step 5: 初始化序列递推求解器
            //         首帧位置设置为原点，将首帧位姿和速度赋值给 propagator 的 initState
            //         从 attitude estimator 提取出从首帧时刻点开始，到最新时刻的 imu 量测，输入到 propagator 让他递推到最新时刻
            IMUMotionState initState(frame_i->p_wb, frame_i->v_wb, frame_i->q_wb);
            RETURN_IF_FALSE(this->InitializePropagator(initState, frame_i->timeStamp));
            // 初始化过程成功，打上标志
            this->status = INITIALIZED;
            return true;
        } else {
            this->status = NEED_INIT;
            return false;
        }
    }


    /* 基于某一帧的多目测量结果进行三角化 */
    bool Backend::TrianglizeMultiView(const std::shared_ptr<Frame> &frame) {
        if (frame == nullptr) {
            return false;
        }
        // TODO: 从 frame->features 中挑选点进行三角化，结果将保存在 feature manager 中
        for (auto it = frame->features.begin(); it != frame->features.end(); ++it) {
            std::vector<Quaternion> all_q_wb;
            std::vector<Vector3> all_p_wb;
            std::vector<Vector2> all_norm;
            // TODO: 
        }
        return true;
    }


    /* 基于三角化成功的点，估计某一帧的位姿 */
    bool Backend::EstimateFramePose(const std::shared_ptr<Frame> &frame) {
        if (frame == nullptr) {
            return false;
        }
        // TODO: 从 frame->features 中挑选三角化成功的点，输入到 pnp solver 中
        return true;
    }


    /* 初始化序列化 propagator */
    bool Backend::InitializePropagator(const IMUMotionState &initState,
                                       const fp64 startTime) {
        // 首帧位置设置为原点，将首帧位姿和速度赋值给 propagator 的 initState
        this->propagator.initState.p_wb = initState.p_wb;
        this->propagator.initState.v_wb = initState.v_wb;
        this->propagator.initState.q_wb = initState.q_wb;
        // 从 attitude estimator 提取出从首帧时刻点开始，到最新时刻的 imu 量测
        auto it = this->attitudeEstimator.items.begin();
        while (std::fabs((*it)->timeStamp - startTime) < this->dataloader.imuPeriod ||
               it != this->attitudeEstimator.items.end()) {
            ++it;
        }
        RETURN_FALSE_IF_EQUAL(it == this->attitudeEstimator.items.end());
        // 依次输入到 propagator 让他递推到最新时刻
        while (it != this->attitudeEstimator.items.end()) {
            this->propagator.Propagate((*it)->accel, (*it)->gyro, (*it)->timeStamp);
            ++it;
        }
        return true;
    }
}