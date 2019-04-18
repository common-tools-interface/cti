/******************************************************************************\
 * cti_split.hpp - Header file for splitting strings.
 *
 * Copyright 2019 Cray Inc.  All Rights Reserved.
 *
 * Unpublished Proprietary Information.
 * This unpublished work is protected to trade secret, copyright and other laws.
 * Except as permitted by contract or express written permission of Cray Inc.,
 * no part of this work or its content may be used, reproduced or disclosed
 * in any form.
 *
 ******************************************************************************/

#pragma once

#include <sstream>
#include <tuple>
#include <utility>

namespace cti {

/* split string into tuple of size determined at complie time. usage:
	std::string s_1, ..., s_N;
	std::tie(s_1, ..., s_N) = split::string<N>(line_to_split, delim = ' ');
*/
namespace split {

	std::string removeLeadingWhitespace(const std::string& str, const std::string& whitespace = " \t") {
		const auto startPos = str.find_first_not_of(whitespace);
		if (startPos == std::string::npos) return "";
		const auto endPos = str.find_last_not_of(whitespace);
		return str.substr(startPos, endPos - startPos + 1);
        }

	namespace { /* iteration tuple helpers */
		// C++14 could use std::index_sequence...
		template<std::size_t I = 0, typename FuncT, typename... Tp>
		inline typename std::enable_if<I == sizeof...(Tp), void>::type
		for_each(std::tuple<Tp...> &, FuncT) {}

		template<std::size_t I = 0, typename FuncT, typename... Tp>
		inline typename std::enable_if<I < sizeof...(Tp), void>::type
		for_each(std::tuple<Tp...>& t, FuncT f) {
			f(std::get<I>(t));
			for_each<I + 1, FuncT, Tp...>(t, f);
		}
	};

	namespace { /* type-generating tuple helpers */
		template<typename, typename>
		struct append_to_type_seq { };

		template<typename T, typename... Ts, template<typename...> class TT>
		struct append_to_type_seq<T, TT<Ts...>> {
			using type = TT<Ts..., T>;
		};

		template<typename T, unsigned int N, template<typename...> class TT>
		struct repeat {
			using type = typename
				append_to_type_seq<
					T,
					typename repeat<T, N-1, TT>::type
					>::type;
		};

		template<typename T, template<typename...> class TT>
		struct repeat<T, 0, TT> {
			using type = TT<>;
		};
	};

	template <std::size_t N>
	using NStringTuple = typename repeat<std::string, N, std::tuple>::type;

	template <std::size_t N>
	NStringTuple<N> string(std::string const& line, char delim = ' ') {
		NStringTuple<N> tup;

		std::stringstream linestream(line);

		for_each(tup, [&](std::string& str_target){
			std::getline(linestream, str_target, delim);
		});

		std::string rest;
		linestream >> rest;
		if (!rest.empty()) {
			std::get<N - 1>(tup).append(delim + rest);
		}

		return tup;
	}

} /* namespace cti::split */

} /* namespace cti */
