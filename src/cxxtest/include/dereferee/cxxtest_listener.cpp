/*
 *	This file is part of Dereferee, the diagnostic checked pointer library.
 *
 *	Dereferee is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	Dereferee is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with Dereferee; if not, write to the Free Software
 *	Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <cstdlib>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <dereferee/listener.h>

#include <cxxtest/MemoryTrackingListener.h>
#include <cxxtest/SafeString.h>

// ===========================================================================
/**
 * The cxxtest_listener class is an implementation of the Dereferee::listener
 * class that maps Dereferee errors to CxxTest test case failures, with output
 * in a plain-text human readable format.
 *
 * To affect runtime behavior, the following options can be used:
 *
 * - "use.stderr": if set to "true", output will be sent to stderr; otherwise,
 *   it will be sent to stdout
 * - "output.prefix": if set, the value of this variable will be prepended to
 *   each line of output generated by the listener. This can be useful for
 *   pulling the output of the listener out of a log and processing it later.
 * - "max.leaks.to.report": if set, the integer value of this variable
 *   will be used to specify the maximum number of memory leaks that should be
 *   reported at the end of execution.
 */

// ===========================================================================
/*
 * Messages corresponding to the error codes used by Dereferee.
 */
static const char* error_messages[] =
{
	"Checked pointers cannot point to memory that wasn't allocated with new or new[]",
	"Assigned dead (never initialized) pointer to another pointer",
	"Assigned dead (already deleted) pointer to another pointer",
	"Assigned dead (out of bounds) pointer to another pointer",
	"Called delete instead of delete[] on array pointer",
	"Called delete[] instead of delete on non-array pointer",
	"Called delete on (never initialized) dead pointer",
	"Called delete[] on (never initialized) dead pointer",
	"Called delete on (already deleted or not dynamically allocated) dead pointer",
	"Called delete[] on (already deleted or not dynamically allocated) dead pointer",
	"Dereferenced (never initialized) dead pointer using operator->",
	"Dereferenced (never initialized) dead pointer using operator*",
	"Dereferenced (never initialized) dead pointer using operator[]",
	"Dereferenced (already deleted) dead pointer using operator->",
	"Dereferenced (already deleted) dead pointer using operator*",
	"Dereferenced (already deleted) dead pointer using operator[]",
	"Dereferenced (out of bounds) dead pointer using operator->",
	"Dereferenced (out of bounds) dead pointer using operator*",
	"Dereferenced (out of bounds) dead pointer using operator[]",
	"Dereferenced null pointer using operator->",
	"Dereferenced null pointer using operator*",
	"Dereferenced null pointer using operator[]",
	"Used (never initialized) dead pointer in an expression",
	"Used (already deleted) dead pointer in an expression",
	"Used (out of bounds) dead pointer in an expression",
	"Used (never initialized) dead pointer in a comparison",
	"Used (already deleted) dead pointer in a comparison",
	"Used (out of bounds) dead pointer in a comparison",
	"Used null pointer on only one side of an inequality comparison; if one side is null then the both sides must be null",
	"Both pointers being compared are alive but point into different memory blocks, so the comparison is undefined",
	"Used (never initialized) dead pointer in an arithmetic expression",
	"Used (already deleted) dead pointer in an arithmetic expression",
	"Used (out of bounds) dead pointer in an arithmetic expression",
	"Used null pointer in an arithmetic expression",
	"Used null pointer on only one side of a pointer subtraction expression; if one side is null then both sides must be null",
	"Both pointers being subtracted are alive but point into different memory blocks, so the distance between them is undefined",
	"Pointer arithmetic has moved a live pointer out of bounds",
	"Used operator[] on a pointer that does not point to an array",
	"Array index %d is out of bounds; valid indices are in the range [0..%zu]",
	"A previous operation has made this pointer invalid"
};

// ===========================================================================
/*
 * Messages corresponding to the warning codes used by Dereferee.
 */
static const char* warning_messages[] =
{
	"Memory leak caused by last live pointer to memory block going out of scope",
	"Memory leak caused by last live pointer to memory block being overwritten",
	"Memory %s allocated block was corrupted, likely due to invalid array indexing or pointer arithmetic"
};

// ===========================================================================
/*
 * Memory block corruption types.
 */
static const char* corruption_messages[] =
{
	"", "before", "after", "before and after"
};

// ===========================================================================

namespace DerefereeSupport
{

/**
 * Interface and implementation of the cxxtest_listener class.
 */
class cxxtest_listener : public Dereferee::listener
{
private:
	const Dereferee::usage_stats* usage_stats;

	char* prefix_string;

	size_t max_leaks;

	FILE* stream;
	
	FILE* webcat_file;

	Dereferee::platform* platform;

	// -----------------------------------------------------------------------
	void prefix_printf(const char* format, ...);

	// -----------------------------------------------------------------------
	void prefix_vprintf(const char* format, va_list args);

	// -----------------------------------------------------------------------
	void print_backtrace(void** backtrace, const char* label);

public:
	// -----------------------------------------------------------------------
	cxxtest_listener(const Dereferee::option* options,
		Dereferee::platform* platform);
	
	// -----------------------------------------------------------------------
	~cxxtest_listener();

	// -----------------------------------------------------------------------
	size_t maximum_leaks_to_report();

	// -----------------------------------------------------------------------
    void* get_allocation_user_info(
        const Dereferee::allocation_info& alloc_info);

	// -----------------------------------------------------------------------
	void begin_report(const Dereferee::usage_stats& stats);

	// -----------------------------------------------------------------------
    bool should_report_leak(const Dereferee::allocation_info& leak);

	// -----------------------------------------------------------------------
	void report_leak(const Dereferee::allocation_info& leak);
	
	// -----------------------------------------------------------------------
	void report_truncated(size_t reports_logged,
			size_t actual_leaks);
	
	// -----------------------------------------------------------------------
	void end_report();
	
	// -----------------------------------------------------------------------
	void error(Dereferee::error_code code, va_list args);

	// -----------------------------------------------------------------------
	void warning(Dereferee::warning_code code, va_list args);
};


// ---------------------------------------------------------------------------
cxxtest_listener::cxxtest_listener(const Dereferee::option* options,
	Dereferee::platform* platform)
{
	// Initialize defaults.
	this->platform = platform;
	stream = stdout;
	prefix_string = NULL;
	max_leaks = UINT_MAX;
	webcat_file = NULL;

	while(options->key != NULL)
	{
		if(strcmp(options->key, "use.stderr") == 0)
		{
			if(strcmp(options->value, "true") == 0)
			{
				stream = stderr;
			}
		}
		else if(strcmp(options->key, "webcat.stats.path") == 0)
		{
			webcat_file = fopen(options->value, "w");
			setvbuf(webcat_file, NULL, _IONBF, 0);
		}
		else if(strcmp(options->key, "output.prefix") == 0)
		{
			size_t len = strlen(options->value);
			prefix_string = (char*)malloc(len + 1);
			strncpy(prefix_string, options->value, len);
		}
		else if(strcmp(options->key, "max.leaks.to.report") == 0)
		{
			max_leaks = atoi(options->value);
		}
		
		options++;
	}

	setvbuf(stream, NULL, _IONBF, 0);
}

// ---------------------------------------------------------------------------
cxxtest_listener::~cxxtest_listener()
{
	if(webcat_file)
		fclose(webcat_file);

	if(prefix_string)
		free(prefix_string);
}

// ------------------------------------------------------------------
size_t cxxtest_listener::maximum_leaks_to_report()
{
	return max_leaks;
}

// ------------------------------------------------------------------
void* cxxtest_listener::get_allocation_user_info(
    const Dereferee::allocation_info& /* alloc_info */)
{
    return (void*) CxxTest::MemoryTrackingListener::tagAction(
        CxxTest::MemoryTrackingListener::GET);
}

// ------------------------------------------------------------------
bool cxxtest_listener::should_report_leak(
	const Dereferee::allocation_info& leak)
{
	uintptr_t tag = (uintptr_t) leak.user_info();
    return (tag & 0x80000000) == 0;
}

// ------------------------------------------------------------------
void cxxtest_listener::begin_report(const Dereferee::usage_stats& stats)
{
	usage_stats = &stats;

	if (webcat_file)
	{
		fprintf(webcat_file,
				"$results->setNumLeaks(%zu);\n", stats.leaks());
	}

	if(stats.leaks() > 0)
	{
		prefix_printf("%zu memory leaks were detected:\n",
			   stats.leaks());
		prefix_printf("--------\n");
	}
	else
	{
		prefix_printf("No memory leaks detected.\n");
	}
}

// ------------------------------------------------------------------
void cxxtest_listener::report_leak(const Dereferee::allocation_info& leak)
{
	prefix_printf("Leaked %zu bytes ", leak.block_size());

	if(leak.type_name())
	{
		char demangled[512] = { '\0' };
		strcpy(demangled, leak.type_name());

		if(leak.is_array())
		{
			if (leak.array_size())
			{
				fprintf(stream, "(%s[%zu]) ", demangled, leak.array_size());
			}
			else
			{
				fprintf(stream, "(%s[]) ", demangled);
			}
		}
		else
		{
			fprintf(stream, "(%s) ", demangled);
		}
	}
	
	fprintf(stream, "at address %p\n", leak.address());

	print_backtrace(leak.backtrace(), "allocated in");

	prefix_printf("\n");
}		

// ------------------------------------------------------------------
void cxxtest_listener::report_truncated(size_t reports_logged,
		size_t actual_leaks)
{
	prefix_printf("\n");
	prefix_printf("(only %zu of %zu leaks shown)\n", reports_logged,
				   actual_leaks);
}

// ------------------------------------------------------------------
void cxxtest_listener::end_report()
{
	prefix_printf("\n");
	prefix_printf("Memory usage statistics:\n");
	prefix_printf("--------\n");
	prefix_printf("Total memory allocated during execution:   "
				   "%zu bytes\n", usage_stats->total_bytes_allocated());
	prefix_printf("Maximum memory in use during execution:    "
				   "%zu bytes\n", usage_stats->maximum_bytes_in_use());
	prefix_printf("Number of calls to new:                    %zu\n",
				   usage_stats->calls_to_new());
	prefix_printf("Number of calls to delete (non-null):      %zu\n",
				   usage_stats->calls_to_delete());
	prefix_printf("Number of calls to new[]:                  %zu\n",
				   usage_stats->calls_to_array_new());
	prefix_printf("Number of calls to delete[] (non-null):    %zu\n",
				   usage_stats->calls_to_array_delete());
	prefix_printf("Number of calls to delete (null):          %zu\n",
				   usage_stats->calls_to_delete_null());
	prefix_printf("Number of calls to delete[] (null):        %zu\n",
				   usage_stats->calls_to_array_delete_null());

	if (webcat_file)
	{
		fprintf(webcat_file,
				"$results->setMemoryAmounts(%zu, %zu);\n",
				usage_stats->total_bytes_allocated(),
				usage_stats->maximum_bytes_in_use());

		fprintf(webcat_file,
				"$results->setNumCalls(%zu, %zu, %zu, %zu, %zu);\n",
				usage_stats->calls_to_new(),
				usage_stats->calls_to_delete(),
				usage_stats->calls_to_array_new(),
				usage_stats->calls_to_array_delete(),
				usage_stats->calls_to_delete_null() +
					usage_stats->calls_to_array_delete_null());
	}
}

// ------------------------------------------------------------------
void cxxtest_listener::error(Dereferee::error_code code, va_list args)
{
	char text[513];
	vsprintf(text, error_messages[code], args);
	
	if (prefix_string)
        CxxTest::__cxxtest_assertmsg =
            CxxTest::SafeString(prefix_string) + text;
    else
        CxxTest::__cxxtest_assertmsg = text;

#ifdef __CYGWIN__
	// Can't use abort() here because it hard-kills the process on Windows,
	// rather than raising a signal that would be caught so execution could
	// continue with the next test case. Instead, cause an access violation
	// that will be caught by the structured exception handler.
	int* x = 0;
	*x = 0xBADBEEF;
#else
	abort();
#endif
}

// ------------------------------------------------------------------
void cxxtest_listener::warning(Dereferee::warning_code code, va_list args)
{
	CxxTest::SafeString str;

	if (prefix_string)
        str += prefix_string;

	char msg[512];

	if(code == Dereferee::warning_memory_boundary_corrupted)
	{
		Dereferee::memory_corruption_location loc =
			(Dereferee::memory_corruption_location)va_arg(args, int);

		sprintf(msg, warning_messages[code], corruption_messages[loc]);
	}
	else
	{
		vsprintf(msg, warning_messages[code], args);
	}

	str += msg;

	if(!CxxTest::__cxxtest_runCompleted)
	{
		CxxTest::doWarn("", 0, str.c_str());
	}
}

// ---------------------------------------------------------------------------
void cxxtest_listener::prefix_printf(const char* format, ...)
{
	if(prefix_string)
		fputs(prefix_string, stream);

	va_list args;
	va_start(args, format);
	vfprintf(stream, format, args);	
	va_end(args);	
}

// ---------------------------------------------------------------------------
void cxxtest_listener::prefix_vprintf(const char* format, va_list args)
{
	if(prefix_string)
		fputs(prefix_string, stream);

	vfprintf(stream, format, args);	
}

// ------------------------------------------------------------------
void cxxtest_listener::print_backtrace(void** backtrace, const char* label)
{
	if(backtrace == NULL)
		return;

	bool first = true;

	char function[DEREFEREE_MAX_FUNCTION_LEN] = { 0 };
	char filename[DEREFEREE_MAX_FILENAME_LEN] = { 0 };
	int line = 0;

	while(*backtrace)
	{
		void *addr = *backtrace;
		
		if(platform->get_backtrace_frame_info(addr,
			function, filename, &line))
        {
            if(CxxTest::filter_backtrace_frame(function))
    		{
    			if(first)
    			{
    				prefix_printf("%14s: ", label);
    				first = false;
    			}
    			else
    				prefix_printf("                ");

    			if(line)
    				printf("%s (%s:%d)\n", function, filename, line);
    			else
    				printf("%s\n", function);
    		}
		}
		
		backtrace++;
	}
}

} // end namespace DerefereeSupport

// ===========================================================================
/*
 * Implementation of the functions called by the Dereferee memory manager to
 * create and destroy the listener object.
 */

Dereferee::listener* Dereferee::create_listener(
		const Dereferee::option* options, Dereferee::platform* platform)
{
	return new DerefereeSupport::cxxtest_listener(options, platform);
}

void Dereferee::destroy_listener(Dereferee::listener* listener)
{
	delete listener;
}
