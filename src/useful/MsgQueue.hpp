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

#define MSG_QUEUE_SIZE			256

#include <sys/types.h>
#include <sys/msg.h>
#include <sys/ipc.h>

#include <string>
#include <type_traits>
#include <stdexcept>

template <typename TagType,
	typename = typename std::enable_if<std::is_convertible<TagType, long>::value>::type>
class MsgQueue {
public:
	struct msg_buffer {
		long mtype;
		char mtext[MSG_QUEUE_SIZE + 1];
	};

	static constexpr const char* DestroyOnShutdown = "destroy";
	static constexpr const char* KeepOnShutdown = "keep";
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

	MsgQueue(key_t k)
		: m_qkey{k}
		, m_qid{msgget(m_qkey, IPC_CREAT | 0600)}
	{
		if (m_qid < 0) {
			throw std::runtime_error("failed to get message queue");
		}
	}

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

	void send(TagType const type, std::string const& msg)
	{
		if (msg.length() > MSG_QUEUE_SIZE) {
			throw std::runtime_error(
				std::string("string too long for message queue (len ") +
				std::to_string(msg.length()) + ")");
		}

		struct msg_buffer msg_buf;

		msg_buf.mtype = long{type};

		strncpy(msg_buf.mtext, msg.c_str(), MSG_QUEUE_SIZE);
		msg_buf.mtext[MSG_QUEUE_SIZE] = '\0';

		if (msgsnd(m_qid, &msg_buf, strlen(msg_buf.mtext) + 1, 0) < 0) {
			throw std::runtime_error("msgsnd failed: " + std::string{strerror(errno)});
		}
	}

	std::string recv(TagType const type)
	{
		struct msg_buffer msg_buf;

		if (msgrcv(m_qid, &msg_buf, MSG_QUEUE_SIZE, long{type}, MSG_NOERROR) < 0) {
			throw std::runtime_error("msgrcv failed: " + std::string{strerror(errno)});
		}

		msg_buf.mtext[MSG_QUEUE_SIZE] = '\0';

		return std::string{msg_buf.mtext};
	}
};
