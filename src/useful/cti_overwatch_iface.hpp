/******************************************************************************\
 * cti_overwatch.h - Header file for the overwatch interface.
 *
 * Copyright 2014 Cray Inc.  All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
 *
 * $HeadURL$
 * $Date$
 * $Rev$
 * $Author$
 *
 ******************************************************************************/

#ifndef _CTI_OVERWATCH_H
#define _CTI_OVERWATCH_H

#ifdef __cplusplus
extern "C" {
#endif

/* struct typedefs */
typedef struct
{
	pid_t	o_pid;		// overwatch process pid	
	FILE *	pipe_r;		// my read stream
	FILE *	pipe_w;		// my write stream
} cti_overwatch_t;

cti_overwatch_t *	_cti_create_overwatch(const char *);
int					_cti_assign_overwatch(cti_overwatch_t *, pid_t);
void				_cti_exit_overwatch(cti_overwatch_t *);

#ifdef __cplusplus
}

#include <signal.h>

#include <memory>

// assign a cti_overwatch handle on construction, tell overwatch to kill pid on destruction
class overwatch_handle
{
public: // types
	using ptr_type = std::unique_ptr<cti_overwatch_t, decltype(&_cti_exit_overwatch)>;

private: // variables
	pid_t m_targetPid;
	ptr_type m_overwatchPtr;

public: // interface
	overwatch_handle(std::string const& overwatchPath, pid_t targetPid)
		: m_targetPid{targetPid}
		, m_overwatchPtr
			{ _cti_create_overwatch(overwatchPath.c_str())
			, _cti_exit_overwatch
		}
	{
		if (_cti_assign_overwatch(m_overwatchPtr.get(), m_targetPid)) {
			throw std::runtime_error("cti_overwatch assignment failed on pid " + std::to_string(targetPid));
		}
	}

	overwatch_handle(overwatch_handle&& moved)
		: m_targetPid{moved.m_targetPid}
		, m_overwatchPtr{std::move(moved.m_overwatchPtr)}
	{
		moved.m_targetPid = pid_t{-1};
	}

	overwatch_handle& operator= (overwatch_handle&& moved)
	{
		m_targetPid    = moved.m_targetPid;
		m_overwatchPtr = std::move(moved.m_overwatchPtr);
		return *this;
	}

	overwatch_handle()
		: m_targetPid{-1}
		, m_overwatchPtr{nullptr, _cti_exit_overwatch}
	{}

	operator bool() const { return (m_targetPid > 0); }

	pid_t getPid() const { return m_targetPid; }
};

#endif

#endif /* _CTI_OVERWATCH_H */

