#include "pose_client.h"
axrb::protocol::PoseFrame& axrb_input_fixture() {
    static axrb::protocol::PoseFrame frame;
    return frame;
}
namespace axrb::runtime {
axrb::protocol::PoseFrame PoseClient::latest_pose_frame() { return axrb_input_fixture(); }
PoseClient& pose_client() { static PoseClient client; return client; }
}
