// Copyright (c) 2020 by Robert Bosch GmbH. All rights reserved.
// Copyright (c) 2020 - 2021 by Apex.AI Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0
#ifndef IOX_HOOFS_POSIX_WRAPPER_UNIX_DOMAIN_SOCKET_HPP
#define IOX_HOOFS_POSIX_WRAPPER_UNIX_DOMAIN_SOCKET_HPP

#include "iceoryx_hoofs/cxx/optional.hpp"
#include "iceoryx_hoofs/design_pattern/creation.hpp"
#include "iceoryx_hoofs/internal/posix_wrapper/ipc_channel.hpp"
#include "iceoryx_hoofs/internal/units/duration.hpp"
#include "iceoryx_platform/fcntl.hpp"
#include "iceoryx_platform/platform_settings.hpp"
#include "iceoryx_platform/socket.hpp"
#include "iceoryx_platform/stat.hpp"
#include "iceoryx_platform/un.hpp"

namespace iox
{
namespace posix
{
/// @brief Wrapper class for unix domain socket
class UnixDomainSocket : public DesignPattern::Creation<UnixDomainSocket, IpcChannelError>
{
  public:
    struct NoPathPrefix_t
    {
    };
    static constexpr NoPathPrefix_t NoPathPrefix{};
    static constexpr uint64_t NULL_TERMINATOR_SIZE = 1U;
    static constexpr uint64_t MAX_MESSAGE_SIZE = platform::IOX_UDS_SOCKET_MAX_MESSAGE_SIZE - NULL_TERMINATOR_SIZE;
    /// @brief The name length is limited by the size of the sockaddr_un::sun_path buffer and the IOX_SOCKET_PATH_PREFIX
    static constexpr size_t LONGEST_VALID_NAME = sizeof(sockaddr_un::sun_path) - 1;

    using UdsName_t = cxx::string<LONGEST_VALID_NAME>;
    using Message_t = cxx::string<MAX_MESSAGE_SIZE>;

    /// @brief for calling private constructor in create method
    friend class DesignPattern::Creation<UnixDomainSocket, IpcChannelError>;

    /// @brief default constructor. The result is an invalid UnixDomainSocket object which can be reassigned later by
    /// using the
    /// move constructor.
    UnixDomainSocket() noexcept;

    UnixDomainSocket(const UnixDomainSocket& other) = delete;
    UnixDomainSocket(UnixDomainSocket&& other) noexcept;
    UnixDomainSocket& operator=(const UnixDomainSocket& other) = delete;
    UnixDomainSocket& operator=(UnixDomainSocket&& other) noexcept;

    ~UnixDomainSocket() noexcept;

    /// @brief unlink the provided unix domain socket
    /// @param name of the unix domain socket to unlink
    /// @return true if the unix domain socket could be unlinked, false otherwise, IpcChannelError if error occured
    static cxx::expected<bool, IpcChannelError> unlinkIfExists(const UdsName_t& name) noexcept;

    /// @brief unlink the provided unix domain socket
    /// @param NoPathPrefix signalling that this method does not add a path prefix
    /// @param name of the unix domain socket to unlink
    /// @return true if the unix domain socket could be unlinked, false otherwise, IpcChannelError if error occured
    static cxx::expected<bool, IpcChannelError> unlinkIfExists(const NoPathPrefix_t, const UdsName_t& name) noexcept;

    /// @brief send a message using std::string.
    /// @param msg to send
    /// @return IpcChannelError if error occured
    cxx::expected<IpcChannelError> send(const std::string& msg) const noexcept;

    /// @brief try to send a message for a given timeout duration using std::string
    /// @param msg to send
    /// @param timout for the send operation
    /// @return IpcChannelError if error occured
    cxx::expected<IpcChannelError> timedSend(const std::string& msg, const units::Duration& timeout) const noexcept;

    /// @brief receive message using std::string.
    /// @return received message. In case of an error, IpcChannelError is returned and msg is empty.
    cxx::expected<std::string, IpcChannelError> receive() const noexcept;

    /// @brief try to receive message for a given timeout duration using std::string.
    /// @param timout for the receive operation
    /// @return received message. In case of an error, IpcChannelError is returned and msg is empty.
    cxx::expected<std::string, IpcChannelError> timedReceive(const units::Duration& timeout) const noexcept;

  private:
    UnixDomainSocket(const IpcChannelName_t& name,
                     const IpcChannelSide channelSide,
                     const size_t maxMsgSize = MAX_MESSAGE_SIZE,
                     const uint64_t maxMsgNumber = 10U) noexcept;

    UnixDomainSocket(const NoPathPrefix_t,
                     const UdsName_t& name,
                     const IpcChannelSide channelSide,
                     const size_t maxMsgSize = MAX_MESSAGE_SIZE,
                     const uint64_t maxMsgNumber = 10U) noexcept;

    cxx::expected<IpcChannelError> destroy() noexcept;

    cxx::expected<IpcChannelError> initalizeSocket() noexcept;

    IpcChannelError convertErrnoToIpcChannelError(const int32_t errnum) const noexcept;

    cxx::expected<IpcChannelError> closeFileDescriptor() noexcept;

  private:
    static constexpr int32_t ERROR_CODE = -1;
    static constexpr int32_t INVALID_FD = -1;

    UdsName_t m_name;
    IpcChannelSide m_channelSide = IpcChannelSide::CLIENT;
    int32_t m_sockfd{INVALID_FD};
    sockaddr_un m_sockAddr{};
    size_t m_maxMessageSize{MAX_MESSAGE_SIZE};
};
} // namespace posix
} // namespace iox

#endif // IOX_HOOFS_POSIX_WRAPPER_UNIX_DOMAIN_SOCKET_HPP
