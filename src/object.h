// Copyright 2025 The NATS Authors
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#define obsErrInvalidStoreName          "invalid object-store name"
#define obsErrBucketExists              "bucket name already in use"
#define obsErrBucketNotFound            "bucket not found"
#define obsErrBadObjectMeta             "object-store meta information invalid"
#define obsErrNameIsRequired            "name is required"
#define obsErrLinkNotAllowed            "link cannot be set when putting the object in bucket"
#define obsErrCantGetBucket             "invalid get operation, object is a link to a bucket"
#define obsErrDigestMismatch            "received a corrupt object, digests do not match"
#define obsErrReadComplete              "no more data to read from the object"
#define obsErrUpdateMetaDelete          "cannot update meta for a deleted or not found object"
#define obsErrObjectAlreadyExists       "an object already exists with that name"
#define obsErrNoLinkToDeleted           "not allowed to link to a deleted object"
#define obsErrNoLinkToLink              "not allowed to link to another link"

#define obsDefaultChunkSize             (uint32_t)(128 * 1024)

#define obsInitialListCapValue          16

extern int obsInitialListCap;
