#include "LicenseClient.h"
#include "Certificates.h"
#include <grpcpp/security/credentials.h>
#include <windows.h>
#include <stdexcept>
#include <string>

constexpr char LicenseClient::kServerAddress[];

LicenseClient::StreamConfigReader::StreamConfigReader(std::unique_ptr<grpc::ClientReader<::capkfa::GetConfigResponse>> reader)
    : reader_(std::move(reader)) {
    if (!reader_) {
        throw std::runtime_error("Failed to create StreamConfig reader");
    }
}

bool LicenseClient::StreamConfigReader::Read(::capkfa::GetConfigResponse& response) {
    return reader_->Read(&response);
}

void LicenseClient::StreamConfigReader::Finish() {
    grpc::Status status = reader_->Finish();
    if (!status.ok()) {
        throw std::runtime_error("StreamConfig failed: " + status.error_message());
    }
}

LicenseClient::LicenseClient() {
    grpc::SslCredentialsOptions ssl_options;

    ssl_options.pem_root_certs = $d_inline(obf_ca_cert); // Trust the custom CA
    ssl_options.pem_private_key = $d_inline(obf_client_key); // Client private key
    ssl_options.pem_cert_chain = $d_inline(obf_client_cert); // Client certificate

    auto credentials = grpc::SslCredentials(ssl_options);
    auto channel = grpc::CreateChannel(kServerAddress, credentials);
    stub_ = ::capkfa::License::NewStub(channel);

    if (!stub_) {
        throw std::runtime_error("Failed to create gRPC stub for " + std::string(kServerAddress));
    }
}

::capkfa::CreateSessionResponse LicenseClient::CreateSession(const ::capkfa::CreateSessionRequest& request) {
    ::capkfa::CreateSessionResponse response;
    grpc::ClientContext context;
    grpc::Status status = stub_->CreateSession(&context, request, &response);
    if (!status.ok()) {
        throw std::runtime_error("CreateSession failed: " + status.error_message());
    }
    return response;
}

::capkfa::GetSessionResponse LicenseClient::GetLatestSession(const ::capkfa::GetSessionRequest& request) {
    ::capkfa::GetSessionResponse response;
    grpc::ClientContext context;
    grpc::Status status = stub_->GetLatestSession(&context, request, &response);
    if (!status.ok()) {
        throw std::runtime_error("GetLatestSession failed: " + status.error_message());
    }
    return response;
}

::capkfa::KillAllSessionsResponse LicenseClient::KillAllSessions(const ::capkfa::KillAllSessionsRequest& request) {
    ::capkfa::KillAllSessionsResponse response;
    grpc::ClientContext context;
    grpc::Status status = stub_->KillAllSessions(&context, request, &response);
    if (!status.ok()) {
        throw std::runtime_error("KillAllSessions failed: " + status.error_message());
    }
    return response;
}

::capkfa::PingResponse LicenseClient::Ping(const ::capkfa::PingRequest& request) {
    ::capkfa::PingResponse response;
    grpc::ClientContext context;
    grpc::Status status = stub_->Ping(&context, request, &response);
    if (!status.ok()) {
        throw std::runtime_error("Ping failed: " + status.error_message());
    }
    return response;
}

::capkfa::GetStatusResponse LicenseClient::GetStatus() {
    google::protobuf::Empty request;
    ::capkfa::GetStatusResponse response;
    grpc::ClientContext context;
    grpc::Status status = stub_->GetStatus(&context, request, &response);
    if (!status.ok()) {
        throw std::runtime_error("GetStatus failed: " + status.error_message());
    }
    return response;
}

::capkfa::GetConfigResponse LicenseClient::GetRemoteConfig(const ::capkfa::GetConfigRequest& request) {
    ::capkfa::GetConfigResponse response;
    grpc::ClientContext context;
    grpc::Status status = stub_->GetRemoteConfig(&context, request, &response);
    if (!status.ok()) {
        throw std::runtime_error("GetRemoteConfig failed: " + status.error_message());
    }
    return response;
}

LicenseClient::StreamConfigReader LicenseClient::StreamConfig(const ::capkfa::GetConfigRequest& request) {
    return StreamConfigReader(stub_->StreamConfig(new grpc::ClientContext, request));
}