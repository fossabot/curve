/*
 *  Copyright (c) 2020 NetEase Inc.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

/*
 * Project: curve
 * Date: Wed Jan 13 09:48:12 CST 2021
 * Author: wuhanqing
 */

#include "src/client/mds_client.h"

#include <brpc/server.h>
#include <glog/logging.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string>

#include "test/client/mock/mock_namespace_service.h"

namespace curve {
namespace client {

using ::testing::_;
using ::testing::DoAll;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::SetArgPointee;

constexpr uint64_t kGiB = 1024ull * 1024 * 1024;

template <typename RpcRequestType, typename RpcResponseType,
          bool RpcFailed = false>
void FakeRpcService(google::protobuf::RpcController* cntl_base,
                    const RpcRequestType* request, RpcResponseType* response,
                    google::protobuf::Closure* done) {
    if (RpcFailed) {
        brpc::Controller* cntl = static_cast<brpc::Controller*>(cntl_base);
        cntl->SetFailed(112, "Not connected to");
    }
    done->Run();
}

class MDSClientTest : public testing::Test {
 protected:
    void SetUp() override {
        const std::string mdsAddr1 = "127.0.0.1:9600";
        const std::string mdsAddr2 = "127.0.0.1:9601";

        ASSERT_EQ(0, server_.AddService(&mockNameService_,
                                        brpc::SERVER_DOESNT_OWN_SERVICE));

        // only start mds on mdsAddr1
        ASSERT_EQ(0, server_.Start(mdsAddr1.c_str(), nullptr));

        option_.rpcRetryOpt.addrs = {mdsAddr2, mdsAddr1};
        option_.rpcRetryOpt.rpcTimeoutMs = 500;            // 500ms
        option_.rpcRetryOpt.maxRPCTimeoutMS = 2000;        // 2s
        option_.rpcRetryOpt.rpcRetryIntervalUS = 1000000;  // 100ms
        option_.mdsMaxRetryMS = 8000;             // 8s
        option_.rpcRetryOpt.maxFailedTimesBeforeChangeAddr = 2;

        ASSERT_EQ(LIBCURVE_ERROR::OK, mdsClient_.Initialize(option_));
    }

    void TearDown() override {
        server_.Stop(0);
        LOG(INFO) << "server stopped";
        server_.Join();
        LOG(INFO) << "server joined";
    }

 protected:
    brpc::Server server_;
    curve::mds::MockNameService mockNameService_;
    MDSClient mdsClient_;
    MetaServerOption option_;
};

TEST_F(MDSClientTest, TestRenameFile) {
    UserInfo userInfo;
    const std::string srcName = "/TestRenameFile";
    const std::string destName = "/TestRenameFile-New";

    // mds return not support
    {
        curve::mds::RenameFileResponse response;
        response.set_statuscode(curve::mds::StatusCode::kNotSupported);
        EXPECT_CALL(mockNameService_, RenameFile(_, _, _, _))
            .WillRepeatedly(DoAll(
                SetArgPointee<2>(response),
                Invoke(FakeRpcService<RenameFileRequest, RenameFileResponse>)));

        auto startMs = TimeUtility::GetTimeofDayMs();
        ASSERT_EQ(LIBCURVE_ERROR::NOT_SUPPORT,
                  mdsClient_.RenameFile(userInfo, srcName, destName));
        auto endMs = TimeUtility::GetTimeofDayMs();
        ASSERT_LE(option_.mdsMaxRetryMS, endMs - startMs);
    }

    // mds return file is occupied
    {
        curve::mds::RenameFileResponse response;
        response.set_statuscode(curve::mds::StatusCode::kFileOccupied);

        EXPECT_CALL(mockNameService_, RenameFile(_, _, _, _))
            .WillRepeatedly(DoAll(
                SetArgPointee<2>(response),
                Invoke(FakeRpcService<RenameFileRequest, RenameFileResponse>)));

        ASSERT_EQ(LIBCURVE_ERROR::FILE_OCCUPIED,
                  mdsClient_.RenameFile(userInfo, srcName, destName));
    }

    // mds first return not support, then success
    {
        curve::mds::RenameFileResponse responseNotSupport;
        responseNotSupport.set_statuscode(
            curve::mds::StatusCode::kNotSupported);
        curve::mds::RenameFileResponse responseOK;
        responseOK.set_statuscode(curve::mds::StatusCode::kOK);

        EXPECT_CALL(mockNameService_, RenameFile(_, _, _, _))
            .WillOnce(DoAll(
                SetArgPointee<2>(responseNotSupport),
                Invoke(FakeRpcService<RenameFileRequest, RenameFileResponse>)))
            .WillOnce(DoAll(
                SetArgPointee<2>(responseOK),
                Invoke(FakeRpcService<RenameFileRequest, RenameFileResponse>)));

        ASSERT_EQ(LIBCURVE_ERROR::OK,
                  mdsClient_.RenameFile(userInfo, srcName, destName));
    }
}

TEST_F(MDSClientTest, TestDeleteFile) {
    UserInfo userInfo;
    const std::string fileName = "/TestDeleteFile";

    // mds return not support
    {
        curve::mds::DeleteFileResponse response;
        response.set_statuscode(curve::mds::StatusCode::kNotSupported);
        EXPECT_CALL(mockNameService_, DeleteFile(_, _, _, _))
            .WillRepeatedly(DoAll(
                SetArgPointee<2>(response),
                Invoke(FakeRpcService<DeleteFileRequest, DeleteFileResponse>)));

        auto startMs = TimeUtility::GetTimeofDayMs();
        ASSERT_EQ(LIBCURVE_ERROR::NOT_SUPPORT,
                  mdsClient_.DeleteFile(fileName, userInfo));
        auto endMs = TimeUtility::GetTimeofDayMs();
        ASSERT_LE(option_.mdsMaxRetryMS, endMs - startMs);
    }

    // mds return file is occupied
    {
        curve::mds::DeleteFileResponse response;
        response.set_statuscode(curve::mds::StatusCode::kFileOccupied);

        EXPECT_CALL(mockNameService_, DeleteFile(_, _, _, _))
            .WillRepeatedly(DoAll(
                SetArgPointee<2>(response),
                Invoke(FakeRpcService<DeleteFileRequest, DeleteFileResponse>)));

        ASSERT_EQ(LIBCURVE_ERROR::FILE_OCCUPIED,
                  mdsClient_.DeleteFile(fileName, userInfo));
    }

    // mds first return not support, then success
    {
        curve::mds::DeleteFileResponse responseNotSupport;
        responseNotSupport.set_statuscode(
            curve::mds::StatusCode::kNotSupported);
        curve::mds::DeleteFileResponse responseOK;
        responseOK.set_statuscode(curve::mds::StatusCode::kOK);

        EXPECT_CALL(mockNameService_, DeleteFile(_, _, _, _))
            .WillOnce(DoAll(
                SetArgPointee<2>(responseNotSupport),
                Invoke(FakeRpcService<DeleteFileRequest, DeleteFileResponse>)))
            .WillOnce(DoAll(
                SetArgPointee<2>(responseOK),
                Invoke(FakeRpcService<DeleteFileRequest, DeleteFileResponse>)));

        ASSERT_EQ(LIBCURVE_ERROR::OK,
                  mdsClient_.DeleteFile(fileName, userInfo));
    }
}

TEST_F(MDSClientTest, TestChangeOwner) {
    UserInfo userInfo;
    const std::string fileName = "/TestChangeOwner";
    const std::string newUser = "newuser";

    // mds return not support
    {
        curve::mds::ChangeOwnerResponse response;
        response.set_statuscode(curve::mds::StatusCode::kNotSupported);
        EXPECT_CALL(mockNameService_, ChangeOwner(_, _, _, _))
            .WillRepeatedly(DoAll(
                SetArgPointee<2>(response),
                Invoke(
                    FakeRpcService<ChangeOwnerRequest, ChangeOwnerResponse>)));

        auto startMs = TimeUtility::GetTimeofDayMs();
        ASSERT_EQ(LIBCURVE_ERROR::NOT_SUPPORT,
                  mdsClient_.ChangeOwner(fileName, newUser, userInfo));
        auto endMs = TimeUtility::GetTimeofDayMs();
        ASSERT_LE(option_.mdsMaxRetryMS, endMs - startMs);
    }

    // mds return file is occupied
    {
        curve::mds::ChangeOwnerResponse response;
        response.set_statuscode(curve::mds::StatusCode::kFileOccupied);

        EXPECT_CALL(mockNameService_, ChangeOwner(_, _, _, _))
            .WillRepeatedly(DoAll(
                SetArgPointee<2>(response),
                Invoke(
                    FakeRpcService<ChangeOwnerRequest, ChangeOwnerResponse>)));

        ASSERT_EQ(LIBCURVE_ERROR::FILE_OCCUPIED,
                  mdsClient_.ChangeOwner(fileName, newUser, userInfo));
    }

    // mds first return not support, then success
    {
        curve::mds::ChangeOwnerResponse responseNotSupport;
        responseNotSupport.set_statuscode(
            curve::mds::StatusCode::kNotSupported);
        curve::mds::ChangeOwnerResponse responseOK;
        responseOK.set_statuscode(curve::mds::StatusCode::kOK);

        EXPECT_CALL(mockNameService_, ChangeOwner(_, _, _, _))
            .WillOnce(DoAll(
                SetArgPointee<2>(responseNotSupport),
                Invoke(
                    FakeRpcService<ChangeOwnerRequest, ChangeOwnerResponse>)))
            .WillOnce(DoAll(
                SetArgPointee<2>(responseOK),
                Invoke(
                    FakeRpcService<ChangeOwnerRequest, ChangeOwnerResponse>)));

        ASSERT_EQ(LIBCURVE_ERROR::OK,
                  mdsClient_.ChangeOwner(fileName, newUser, userInfo));
    }
}

TEST_F(MDSClientTest, TestOpenFile) {
    const std::string fileName = "/TestOpenFile";
    UserInfo userInfo;
    userInfo.owner = "test";

    FInfo fileInfo;
    LeaseSession session;

    // rpc always failed
    {
        EXPECT_CALL(mockNameService_, OpenFile(_, _, _, _))
            .WillRepeatedly(Invoke(
                FakeRpcService<OpenFileRequest, OpenFileResponse, true>));

        auto startMs = TimeUtility::GetTimeofDayMs();
        ASSERT_EQ(LIBCURVE_ERROR::FAILED,
                  mdsClient_.OpenFile(fileName, userInfo, &fileInfo, &session));
        auto endMs = TimeUtility::GetTimeofDayMs();
        ASSERT_LE(option_.mdsMaxRetryMS, endMs - startMs);
    }

    // rpc response failed
    {
        curve::mds::OpenFileResponse response;
        response.set_statuscode(curve::mds::StatusCode::kFileNotExists);

        EXPECT_CALL(mockNameService_, OpenFile(_, _, _, _))
            .WillRepeatedly(DoAll(
                SetArgPointee<2>(response),
                Invoke(FakeRpcService<OpenFileRequest, OpenFileResponse>)));

        ASSERT_EQ(LIBCURVE_ERROR::FAILED,
                  mdsClient_.OpenFile(fileName, userInfo, &fileInfo, &session));
    }

    // open normal file success
    {
        curve::mds::OpenFileResponse response;
        response.set_statuscode(curve::mds::StatusCode::kOK);
        response.set_allocated_fileinfo(new curve::mds::FileInfo());

        auto* protoSession = new curve::mds::ProtoSession();
        protoSession->set_sessionid("1");
        protoSession->set_leasetime(1);
        protoSession->set_createtime(1);
        protoSession->set_sessionstatus(curve::mds::SessionStatus::kSessionOK);

        response.set_allocated_protosession(protoSession);

        EXPECT_CALL(mockNameService_, OpenFile(_, _, _, _))
            .WillRepeatedly(DoAll(
                SetArgPointee<2>(response),
                Invoke(FakeRpcService<OpenFileRequest, OpenFileResponse>)));

        ASSERT_EQ(LIBCURVE_ERROR::OK,
                  mdsClient_.OpenFile(fileName, userInfo, &fileInfo, &session));
    }

    // open a flattened clone file
    {
        curve::mds::OpenFileResponse response;
        response.set_statuscode(curve::mds::StatusCode::kOK);

        auto* protoFileInfo = new curve::mds::FileInfo();
        protoFileInfo->set_clonesource("/clone");
        protoFileInfo->set_filestatus(curve::mds::FileStatus::kFileCloned);

        auto* protoSession = new curve::mds::ProtoSession();
        protoSession->set_sessionid("1");
        protoSession->set_leasetime(1);
        protoSession->set_createtime(1);
        protoSession->set_sessionstatus(curve::mds::SessionStatus::kSessionOK);

        response.set_allocated_fileinfo(protoFileInfo);
        response.set_allocated_protosession(protoSession);

        EXPECT_CALL(mockNameService_, OpenFile(_, _, _, _))
            .WillRepeatedly(DoAll(
                SetArgPointee<2>(response),
                Invoke(FakeRpcService<OpenFileRequest, OpenFileResponse>)));

        ASSERT_EQ(LIBCURVE_ERROR::OK,
                  mdsClient_.OpenFile(fileName, userInfo, &fileInfo, &session));
    }

    // open clone file, but response doesn't contains clone source segment
    {
        curve::mds::OpenFileResponse response;
        response.set_statuscode(curve::mds::StatusCode::kOK);

        auto* protoFileInfo = new curve::mds::FileInfo();
        protoFileInfo->set_clonesource("/clone");
        protoFileInfo->set_filestatus(
            curve::mds::FileStatus::kFileCloneMetaInstalled);

        auto* protoSession = new curve::mds::ProtoSession();
        protoSession->set_sessionid("1");
        protoSession->set_leasetime(1);
        protoSession->set_createtime(1);
        protoSession->set_sessionstatus(curve::mds::SessionStatus::kSessionOK);

        response.set_allocated_fileinfo(protoFileInfo);
        response.set_allocated_protosession(protoSession);

        EXPECT_CALL(mockNameService_, OpenFile(_, _, _, _))
            .WillRepeatedly(DoAll(
                SetArgPointee<2>(response),
                Invoke(FakeRpcService<OpenFileRequest, OpenFileResponse>)));

        ASSERT_EQ(LIBCURVE_ERROR::OK,
                  mdsClient_.OpenFile(fileName, userInfo, &fileInfo, &session));
    }

    // open clone file success
    {
        curve::mds::OpenFileResponse response;
        response.set_statuscode(curve::mds::StatusCode::kOK);

        auto* protoFileInfo = new curve::mds::FileInfo();
        protoFileInfo->set_clonesource("/clone");
        protoFileInfo->set_clonelength(10 * kGiB);
        protoFileInfo->set_filestatus(
            curve::mds::FileStatus::kFileCloneMetaInstalled);

        auto* protoSession = new curve::mds::ProtoSession();
        protoSession->set_sessionid("1");
        protoSession->set_leasetime(1);
        protoSession->set_createtime(1);
        protoSession->set_sessionstatus(curve::mds::SessionStatus::kSessionOK);

        auto* cloneSourceSegment = new curve::mds::CloneSourceSegment();
        cloneSourceSegment->set_segmentsize(1ull * 1024 * 1024 * 1024);
        cloneSourceSegment->add_allocatedsegmentoffset(0 * kGiB);
        cloneSourceSegment->add_allocatedsegmentoffset(1 * kGiB);
        cloneSourceSegment->add_allocatedsegmentoffset(9 * kGiB);

        response.set_allocated_fileinfo(protoFileInfo);
        response.set_allocated_protosession(protoSession);
        response.set_allocated_clonesourcesegment(cloneSourceSegment);

        EXPECT_CALL(mockNameService_, OpenFile(_, _, _, _))
            .WillRepeatedly(DoAll(
                SetArgPointee<2>(response),
                Invoke(FakeRpcService<OpenFileRequest, OpenFileResponse>)));

        ASSERT_EQ(LIBCURVE_ERROR::OK,
                  mdsClient_.OpenFile(fileName, userInfo, &fileInfo, &session));

        ASSERT_EQ(fileInfo.sourceInfo.name, "/clone");
        ASSERT_EQ(fileInfo.sourceInfo.length, 10 * kGiB);
        ASSERT_EQ(fileInfo.sourceInfo.segmentSize, 1 * kGiB);
        ASSERT_EQ(fileInfo.sourceInfo.allocatedSegmentOffsets,
                  std::unordered_set<uint64_t>({0 * kGiB, 1 * kGiB, 9 * kGiB}));
    }
}

}  // namespace client
}  // namespace curve
