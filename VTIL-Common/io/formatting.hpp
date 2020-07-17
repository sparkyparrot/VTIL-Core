// Copyright (c) 2020 Can Boluk and contributors of the VTIL Project   
// All rights reserved.   
//    
// Redistribution and use in source and binary forms, with or without   
// modification, are permitted provided that the following conditions are met: 
//    
// 1. Redistributions of source code must retain the above copyright notice,   
//    this list of conditions and the following disclaimer.   
// 2. Redistributions in binary form must reproduce the above copyright   
//    notice, this list of conditions and the following disclaimer in the   
//    documentation and/or other materials provided with the distribution.   
// 3. Neither the name of VTIL Project nor the names of its contributors
//    may be used to endorse or promote products derived from this software 
//    without specific prior written permission.   
//    
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE   
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE  
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE   
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR   
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF   
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS   
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN   
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)   
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE  
// POSSIBILITY OF SUCH DAMAGE.        
//
#pragma once
#include <string>
#include <cstring>
#include <cstdio>
#include <type_traits>
#include <chrono>
#include <optional>
#include <concepts>
#include "../util/lt_typeid.hpp"
#include "../util/dynamic_size.hpp"
#include "../util/type_helpers.hpp"

// [Configuration]
// Determine the way we format the instructions.
//
#ifndef VTIL_FMT_DEFINED
	#define VTIL_FMT_INS_MNM	"%-8s"
	#define VTIL_FMT_INS_OPR	"%-12s"
	#define VTIL_FMT_INS_MNM_S	8
	#define VTIL_FMT_INS_OPR_S	12
	#define VTIL_FMT_SUFFIX_1	'b'
	#define VTIL_FMT_SUFFIX_2	'w'
	#define VTIL_FMT_SUFFIX_4	'd'
	#define VTIL_FMT_SUFFIX_8	'q'
	#define VTIL_FMT_DEFINED
#endif

// Determine RTTI support.
//
#if defined(_CPPRTTI)
	#define HAS_RTTI	_CPPRTTI
#elif defined(__GXX_RTTI)
	#define HAS_RTTI	__GXX_RTTI
#elif defined(__has_feature)
	#define HAS_RTTI	__has_feature(cxx_rtti)
#else
	#define HAS_RTTI	0
#endif

namespace vtil::format
{
	namespace impl
	{
		// Returns a temporary but valid const (w)char* for the given std::(w)string.
		//
		template<typename T>
		static T* buffer_string( std::basic_string<T>&& value )
		{
			static thread_local std::basic_string<T> ring_buffer[ 16 ];
			static thread_local int index = 0;

			auto& ref = ring_buffer[ index ];
			ref = std::move( value );
			index = ++index % std::size( ring_buffer );
			return ref.data();
		}

		// Fixes the type name to be more friendly.
		//
		static std::string fix_type_name( std::string&& in )
		{
			static constexpr const char* remove_list[] = {
				"struct ",
				"class ",
				"enum ",
				"vtil::"
			};
			for ( const char* str : remove_list )
			{
				if ( in.starts_with( str ) )
					return fix_type_name( in.substr( strlen( str ) ) );
				for ( size_t i = 0; i < in.size(); i++ )
				{
					if ( in[ i ] == '<' && in.substr( i + 1 ).starts_with( str ) )
						in = in.substr( 0, i + 1 ) + in.substr( i + 1 + strlen( str ) );
				}
			}
			return in;
		}
	};

	// Suffixes used to indicate registers of N bytes.
	//
	static constexpr char suffix_map[] = { 0, VTIL_FMT_SUFFIX_1, VTIL_FMT_SUFFIX_2, 0, VTIL_FMT_SUFFIX_4, 0, 0, 0, VTIL_FMT_SUFFIX_8 };

	// Returns the type name of the object passed, dynamic type name will
	// redirect to static type name if RTTI is not supported.
	//
	template<typename T>
	static std::string static_type_name()
	{
#if HAS_RTTI
		static std::string res = impl::fix_type_name( typeid( T ).name() );
		return res;
#else
		char buf[ 32 ];
		sprintf_s( buf, "Type%llx", lt_typeid<T>::value );
		return buf;
#endif
	}
	template<typename T>
	static std::string dynamic_type_name( const T& o )
	{
#if HAS_RTTI
		return impl::fix_type_name( typeid( o ).name() );
#else
		return static_type_name<T>();
#endif
	}

	// VTIL string-convertable types implement [std::string T::to_string() const];
	//
	template<typename T>
	concept CustomStringConvertible = requires( T v ) { v.to_string(); };

	// Checks if std::to_string is specialized to convert type into string.
	//
	template<typename T>
	concept StdStringConvertible = requires( T v ) { std::to_string( v ); };

	// Converts any given object to a string.
	//
	template<typename T>
	static auto as_string( const T& x )
	{
		using base_type = std::decay_t<T>;

		if constexpr ( StdStringConvertible<T> )
		{
			return std::to_string( x );
		}
		else if constexpr ( CustomStringConvertible<T> )
		{
			return x.to_string();
		}
		else if constexpr ( std::is_same_v<base_type, std::string> || 
							std::is_same_v<base_type, const char*> )
		{
			return std::string{ x };
		}
		else if constexpr ( std::is_same_v<base_type, std::wstring> )
		{
			return std::string{ x.begin(), x.end() };
		}
		else if constexpr ( std::is_same_v<base_type, const wchar_t*> )
		{
			return std::string{ x, x + wcslen( x ) };
		}
		else if constexpr ( is_specialization_v<std::chrono::duration, base_type> )
		{
			static constexpr auto flt2str = [ ] ( float f ) -> std::string
			{
				char buffer[ 32 ];
				snprintf( buffer, 32, "%.2f", f );
				return buffer;
			};

			static constexpr std::tuple<base_type, const char*, bool> durations[] = 
			{
				{ std::chrono::duration_cast<base_type>( std::chrono::hours{ 1 } ),        "hrs",  false },
				{ std::chrono::duration_cast<base_type>( std::chrono::minutes{ 1 } ),      "min",  false },
				{ std::chrono::duration_cast<base_type>( std::chrono::seconds{ 1 } ),      "sec",  false },
				{ std::chrono::duration_cast<base_type>( std::chrono::milliseconds{ 1 } ), "ms",   false },
				{ std::chrono::duration_cast<base_type>( std::chrono::nanoseconds{ 1 } ),  "ns",   true  },
			};

			for ( auto& [dur, name, last] : durations )
				if ( last || x > dur )
					return flt2str( x.count() / float( dur.count() ) ) + name;
			unreachable();
		}
		// If optional:
		//
		else if constexpr ( is_specialization_v<std::optional, base_type> )
		{
			if ( StringConvertible<decltype( x.value() )> )
			{
				if ( x.has_value() )
					return as_string( x.value() );
				else
					return std::string{ "nullopt" };
			}
		}
		// If container:
		//
		else if constexpr ( Iterable<T> )
		{
			if constexpr ( StringConvertible<decltype( *std::begin( x ) )> )
			{
				std::string items = {};
				for ( auto& entry : x )
					items += as_string( entry ) + ", ";
				if ( !items.empty() ) items.resize( items.size() - 2 );
				return "{" + items + "}";
			}
		}
		// void
	}

	// Disjunction of all possible conversions.
	//
	template<typename T>
	concept StringConvertible = requires( T v ) { !std::is_void_v<decltype( as_string( v ) )>; };

	// Used to fix std::(w)string usage in combination with "%(l)s".
	//
	template<typename T>
	inline static auto fix_parameter( T&& x )
	{
		using base_type = std::remove_cvref_t<T>;

		// If fundamental type, return as is.
		//
		if constexpr ( std::is_fundamental_v<base_type> || std::is_enum_v<base_type> || 
					   std::is_pointer_v<base_type> || std::is_array_v<base_type> )
		{
			return x;
		}
		// If it is a basic string:
		//
		else if constexpr ( std::is_same_v<base_type, std::string> || std::is_same_v<base_type, std::wstring> )
		{
			// If it is a reference, invoke ::data()
			//
			if constexpr ( std::is_reference_v<T> )
				return x.data();
			// Otherwise call buffer helper.
			//
			else
				return impl::buffer_string( std::move( x ) );
		}
		// If string convertible:
		//
		else if constexpr ( StringConvertible<T> )
		{
			return impl::buffer_string( as_string( std::forward<T>( x ) ) );
		}
		// If none matched, forcefully convert into [type @ pointer].
		//
		else
		{
			char buffer[ 32 ];
			snprintf( buffer, 32, "%p", &x );
			return impl::buffer_string( "[" + dynamic_type_name( x ) + "@" + std::string( buffer ) + "]" );
		}
	}

	// Returns formatted string according to <fms>.
	//
	template<typename... params>
	static std::string str( const char* fmt, params&&... ps )
	{
		std::string buffer;
		buffer.resize( snprintf( nullptr, 0, fmt, fix_parameter( ps )... ) );
		snprintf( buffer.data(), buffer.size() + 1, fmt, fix_parameter<params>( std::forward<params>( ps ) )... );
		return buffer;
	}

	// Formats the integer into a signed hexadecimal.
	//
	template<typename T, std::enable_if_t<std::is_integral_v<std::remove_cvref_t<T>>, int> = 0>
	static std::string hex( T&& value )
	{
		if constexpr ( !std::is_signed_v<std::remove_cvref_t<T>> )
		{
			return str( "0x%llx", value );
		}
		else
		{
			if ( value >= 0 ) return str( "0x%llx", value );
			else              return str( "-0x%llx", -value );
		}
	}

	// Formats the integer into a signed hexadecimal with explicit + if positive.
	//
	static std::string offset( int64_t value )
	{
		if ( value >= 0 ) return str( "+ 0x%llx", value );
		else              return str( "- 0x%llx", -value );
	}
};
#undef HAS_RTTI

// Export the concepts.
//
namespace vtil
{
	using format::CustomStringConvertible;
	using format::StdStringConvertible;
	using format::StringConvertible;
};