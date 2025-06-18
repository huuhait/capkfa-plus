#ifndef LICENSE_CLIENT_H
#define LICENSE_CLIENT_H

#include <grpcpp/grpcpp.h>
#include "proto/license.grpc.pb.h"
#include "proto/license.pb.h"
#include <memory>

class LicenseClient {
public:
    LicenseClient();
    ~LicenseClient() = default;

    ::capkfa::CreateSessionResponse CreateSession(const ::capkfa::CreateSessionRequest& request);
    ::capkfa::GetSessionResponse GetLatestSession(const ::capkfa::GetSessionRequest& request);
    ::capkfa::KillAllSessionsResponse KillAllSessions(const ::capkfa::KillAllSessionsRequest& request);
    ::capkfa::PingResponse Ping(const ::capkfa::PingRequest& request);
    ::capkfa::GetStatusResponse GetStatus();
    ::capkfa::GetConfigResponse GetRemoteConfig(const ::capkfa::GetConfigRequest& request);

    class StreamConfigReader {
    public:
        StreamConfigReader(std::unique_ptr<grpc::ClientReader<::capkfa::GetConfigResponse>> reader);
        bool Read(::capkfa::GetConfigResponse& response);
        void Finish();

    private:
        std::unique_ptr<grpc::ClientReader<::capkfa::GetConfigResponse>> reader_;
        grpc::ClientContext context_;
    };

    StreamConfigReader StreamConfig(const ::capkfa::GetConfigRequest& request);

private:
    std::unique_ptr<::capkfa::License::Stub> stub_;
    static constexpr char kServerAddress[] = "license.vietgang.club:443";
};

#endif // LICENSE_CLIENT_H