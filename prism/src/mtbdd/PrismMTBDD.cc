//==============================================================================
//	
//	Copyright (c) 2002-
//	Authors:
//	* Dave Parker <david.parker@comlab.ox.ac.uk> (University of Oxford, formerly University of Birmingham)
//	
//------------------------------------------------------------------------------
//	
//	This file is part of PRISM.
//	
//	PRISM is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.
//	
//	PRISM is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//	
//	You should have received a copy of the GNU General Public License
//	along with PRISM; if not, write to the Free Software Foundation,
//	Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//	
//==============================================================================

#include "PrismMTBDD.h"
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include <limits.h>
#include <util.h>
#include <cudd.h>
#include <dd.h>
#include "PrismMTBDDGlob.h"
#include "jnipointer.h"

#define MAX_LOG_STRING_LEN 1024
#define MAX_ERR_STRING_LEN 1024

//------------------------------------------------------------------------------
// mtbdd engine global variables
//------------------------------------------------------------------------------

// cudd manager
DdManager *ddman;

// logs
// global refs to log classes
static jclass main_log_cls = NULL;
static jclass tech_log_cls = NULL;
// global refs to log objects
static jobject main_log_obj = NULL;
static jobject tech_log_obj = NULL;
// method ids for print method in logs
static jmethodID main_log_mid = NULL;
static jmethodID tech_log_mid = NULL;

// numerical method stuff
int lin_eq_method;
double lin_eq_method_param;
int term_crit;
double term_crit_param;
int max_iters;

// export stuff
int export_type;
FILE *export_file;
JNIEnv *export_env;

// use steady-state detection for transient computation?
bool do_ss_detect;

// error message
char error_message[MAX_ERR_STRING_LEN];

//------------------------------------------------------------------------------
// cudd manager
//------------------------------------------------------------------------------

JNIEXPORT void JNICALL Java_mtbdd_PrismMTBDD_PM_1SetCUDDManager(JNIEnv *env, jclass cls, jlong __jlongpointer ddm)
{
	ddman = jlong_to_DdManager(ddm);
}

//------------------------------------------------------------------------------
// logs
//------------------------------------------------------------------------------

JNIEXPORT void JNICALL Java_mtbdd_PrismMTBDD_PM_1SetMainLog(JNIEnv *env, jclass cls, jobject log)
{
	// if main log has been set previously, we need to delete existing global refs first
	if (main_log_obj != NULL) {
		env->DeleteGlobalRef(main_log_cls);
		env->DeleteGlobalRef(main_log_obj);
	}
	
	// make a global reference to the log object
	main_log_obj = env->NewGlobalRef(log);
	// get the log class and make a global reference to it
	main_log_cls = (jclass)env->NewGlobalRef(env->GetObjectClass(main_log_obj));
	// get the method id for the print method
	main_log_mid = env->GetMethodID(main_log_cls, "print", "(Ljava/lang/String;)V");
}

//------------------------------------------------------------------------------

JNIEXPORT void JNICALL Java_mtbdd_PrismMTBDD_PM_1SetTechLog(JNIEnv *env, jclass cls, jobject log)
{
	// if tech log has been set previously, we need to delete existing global refs first
	if (tech_log_obj != NULL) {
		env->DeleteGlobalRef(tech_log_cls);
		env->DeleteGlobalRef(tech_log_obj);
	}
	
	// make a global reference to the log object
	tech_log_obj = env->NewGlobalRef(log);
	// get the log class and make a global reference to it
	tech_log_cls = (jclass)env->NewGlobalRef(env->GetObjectClass(tech_log_obj));
	// get the method id for the print method
	tech_log_mid = env->GetMethodID(tech_log_cls, "print", "(Ljava/lang/String;)V");
}

//------------------------------------------------------------------------------

void PM_PrintToMainLog(JNIEnv *env, const char *str, ...)
{
	va_list argptr;
	char full_string[MAX_LOG_STRING_LEN];
	
	va_start(argptr, str);
	vsnprintf(full_string, MAX_LOG_STRING_LEN, str, argptr);
	va_end(argptr);
	
	if (env)
		env->CallVoidMethod(main_log_obj, main_log_mid, env->NewStringUTF(full_string));
	else
		printf("%s", full_string);
}

//------------------------------------------------------------------------------

void PM_PrintToTechLog(JNIEnv *env, const char *str, ...)
{
	va_list argptr;
	char full_string[MAX_LOG_STRING_LEN];
	
	va_start(argptr, str);
	vsnprintf(full_string, MAX_LOG_STRING_LEN, str, argptr);
	va_end(argptr);
	
	if (env)
		env->CallVoidMethod(tech_log_obj, tech_log_mid, env->NewStringUTF(full_string));
	else
		printf("%s", full_string);
}

//------------------------------------------------------------------------------
// numerical method stuff
//------------------------------------------------------------------------------

JNIEXPORT void JNICALL Java_mtbdd_PrismMTBDD_PM_1SetLinEqMethod(JNIEnv *env, jclass cls, jint i)
{
	lin_eq_method = i;
}

//------------------------------------------------------------------------------

JNIEXPORT void JNICALL Java_mtbdd_PrismMTBDD_PM_1SetLinEqMethodParam(JNIEnv *env, jclass cls, jdouble d)
{
	lin_eq_method_param = d;
}

//------------------------------------------------------------------------------

JNIEXPORT void JNICALL Java_mtbdd_PrismMTBDD_PM_1SetTermCrit(JNIEnv *env, jclass cls, jint i)
{
	term_crit = i;
}

//------------------------------------------------------------------------------

JNIEXPORT void JNICALL Java_mtbdd_PrismMTBDD_PM_1SetTermCritParam(JNIEnv *env, jclass cls, jdouble d)
{
	term_crit_param = d;
}

//------------------------------------------------------------------------------

JNIEXPORT void JNICALL Java_mtbdd_PrismMTBDD_PM_1SetMaxIters(JNIEnv *env, jclass cls, jint i)
{
	max_iters = i;
}

//------------------------------------------------------------------------------
// export stuff
//------------------------------------------------------------------------------

// store export info globally
// returns 0 on failure, 1 otherwise

int store_export_info(int type, jstring fn, JNIEnv *env)
{
	export_type = type;
	if (fn) {
		const char *filename = env->GetStringUTFChars(fn, 0);
		export_file = fopen(filename, "w");
		if (!export_file) {
			env->ReleaseStringUTFChars(fn, filename);
			return 0;
		}
		env->ReleaseStringUTFChars(fn, filename);
	} else {
		export_file = NULL;
	}
	export_env = env;
	return 1;
}

//------------------------------------------------------------------------------

// export string (either to file or main log)

void export_string(const char *str, ...)
{
	va_list argptr;
	char full_string[MAX_LOG_STRING_LEN];
	
	va_start(argptr, str);
	vsnprintf(full_string, MAX_LOG_STRING_LEN, str, argptr);
	va_end(argptr);
	
	if (export_file) {
		fprintf(export_file, full_string);
	} else {
		PM_PrintToMainLog(export_env, full_string);
	}
}

//------------------------------------------------------------------------------
// use steady-state detection?
//------------------------------------------------------------------------------

JNIEXPORT void JNICALL Java_mtbdd_PrismMTBDD_PM_1SetDoSSDetect(JNIEnv *env, jclass cls, jboolean b)
{
	do_ss_detect = b;
}

//------------------------------------------------------------------------------
// error message handling
//------------------------------------------------------------------------------

void PM_SetErrorMessage(const char *str, ...)
{
	va_list argptr;
	
	va_start(argptr, str);
	vsnprintf(error_message, MAX_LOG_STRING_LEN, str, argptr);
	va_end(argptr);
}

char *PM_GetErrorMessage()
{
	return error_message;
}

JNIEXPORT jstring JNICALL Java_mtbdd_PrismMTBDD_PM_1GetErrorMessage(JNIEnv *env, jclass cls)
{
	return env->NewStringUTF(error_message);
}

//------------------------------------------------------------------------------
// tidy up
//------------------------------------------------------------------------------

JNIEXPORT void JNICALL Java_mtbdd_PrismMTBDD_PM_1FreeGlobalRefs(JNIEnv *env, jclass cls)
{
	// delete all global references
	env->DeleteGlobalRef(main_log_cls);
	env->DeleteGlobalRef(tech_log_cls);
	env->DeleteGlobalRef(main_log_obj);
	env->DeleteGlobalRef(tech_log_obj);
}

//------------------------------------------------------------------------------
