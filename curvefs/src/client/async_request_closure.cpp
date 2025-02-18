/*
 *  Copyright (c) 2022 NetEase Inc.
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
 * Date: Friday Apr 22 17:04:35 CST 2022
 * Author: wuhanqing
 */

#include "curvefs/src/client/async_request_closure.h"

#include <bthread/mutex.h>

#include <memory>
#include <mutex>

#include "curvefs/src/client/error_code.h"
#include "curvefs/src/client/inode_wrapper.h"

namespace curvefs {
namespace client {

namespace internal {

AsyncRequestClosureBase::AsyncRequestClosureBase(
    const std::shared_ptr<InodeWrapper>& inode)
    : inode_(inode) {}

AsyncRequestClosureBase::~AsyncRequestClosureBase() = default;

}  // namespace internal

UpdateVolumeExtentClosure::UpdateVolumeExtentClosure(
    const std::shared_ptr<InodeWrapper>& inode,
    bool sync)
    : AsyncRequestClosureBase(inode), sync_(sync) {}

CURVEFS_ERROR UpdateVolumeExtentClosure::Wait() {
    assert(sync_);

    std::unique_lock<bthread::Mutex> lk(mtx_);
    while (!done_) {
        cond_.wait(lk);
    }

    return MetaStatusCodeToCurvefsErrCode(GetStatusCode());
}

void UpdateVolumeExtentClosure::Run() {
    auto st = GetStatusCode();
    if (st != MetaStatusCode::OK && st != MetaStatusCode::NOT_FOUND) {
        LOG(ERROR) << "UpdateVolumeExtent failed, error: "
                   << MetaStatusCode_Name(st)
                   << ", inodeid: " << inode_->GetInodeId();
        inode_->MarkInodeError();
    }

    inode_->syncingVolumeExtentsMtx_.unlock();

    if (sync_) {
        std::lock_guard<bthread::Mutex> lk(mtx_);
        done_ = true;
        cond_.notify_one();
    } else {
        delete this;
    }
}

}  // namespace client
}  // namespace curvefs
