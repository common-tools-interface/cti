/******************************************************************************\
 * MsgQueue.hpp - Kernel messagequeue helper object
 *
 * Copyright 2015-2017 Cray Inc.  All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
 *
 ******************************************************************************/

#pragma once

#include <sys/types.h>
#include <sys/msg.h>
#include <sys/ipc.h>

#include <string>
#include <type_traits>
#include <stdexcept>
#include <tuple>

#ifndef MSGMAX
#define MSGMAX 2048
#endif

template <typename TagType, typename DataType,
	// ensure tag type can be represented as long
	typename = typename std::enable_if<std::is_convertible<TagType, long>::value>::type,
	// ensure data type struct can be copied as raw chars
	typename = typename std::enable_if<std::is_trivially_copyable<DataType>::value>::type>
class MsgQueue {
public:
	struct msg_buffer {
		TagType  m_type;
		DataType m_data;
	};
private:
	key_t m_qkey;
	int   m_qid;

	// make MsgQueue moveable but not copyable
	MsgQueue(const MsgQueue&) = delete;
	MsgQueue& operator=(const MsgQueue&) = delete;

public:
	MsgQueue(MsgQueue&& other) noexcept
		: m_qkey{other.m_qkey}
		, m_qid{other.m_qid}
	{
		other.m_qid = -1;
	}

	MsgQueue& operator= (MsgQueue&& other) noexcept
	{
		m_qkey = other.m_qkey;
		m_qid  = other.m_qid;

		other.m_qid = -1;

		return *this;
	}

	MsgQueue(key_t k)
		: m_qkey{k}
		, m_qid{msgget(m_qkey, IPC_CREAT | 0600)}
	{
		// ensure messages fit inside the kernel queue size limit
		static_assert(sizeof(long) + sizeof(DataType) <= MSGMAX);

		if (m_qid < 0) {
			throw std::runtime_error("failed to get message queue");
		}
	}

	MsgQueue()
		: m_qkey{}
		, m_qid{-1}
	{}

	operator bool() const { return !(m_qid < 0); }

	void deregister()
	{
		if (m_qid < 0) {
			throw std::runtime_error("message queue has already been deregistered");
		}

		if (msgctl(m_qid, IPC_RMID, NULL) < 0) {
			throw std::runtime_error("msgctl failed: " + std::string{strerror(errno)});
		}

		m_qid = -1;
	}

	void send(TagType const type, DataType const& data)
	{
		msg_buffer msg_buf
			{ .m_type = type
			, .m_data = data
		};

		if (msgsnd(m_qid, &msg_buf, sizeof(msg_buf.m_data), 0) < 0) {
			throw std::runtime_error("msgsnd failed: " + std::string{strerror(errno)});
		}
	}

	std::pair<TagType, DataType> recv()
	{
		struct msg_buffer msg_buf;

		int recv = msgrcv(m_qid, &msg_buf, sizeof(DataType), 0, MSG_NOERROR);
		if ((errno != EIDRM) && (recv < 0)) {
			throw std::runtime_error("msgrcv failed: " + std::string{strerror(errno)});
		}

		return std::make_pair(msg_buf.m_type, msg_buf.m_data);
	}

	DataType recv(TagType const type)
	{
		struct msg_buffer msg_buf;

		int recv = msgrcv(m_qid, &msg_buf, sizeof(DataType), long{type}, MSG_NOERROR);
		if ((errno != EIDRM) && (recv < 0)) {
			throw std::runtime_error("msgrcv failed: " + std::string{strerror(errno)});
		}

		return msg_buf.m_data;
	}
};
