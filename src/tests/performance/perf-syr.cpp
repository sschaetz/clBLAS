/* ************************************************************************
 * Copyright 2013 Advanced Micro Devices, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 * ************************************************************************/


/*
 * Syr performance test cases
 */

#include <stdlib.h>             // srand()
#include <string.h>             // memcpy()
#include <gtest/gtest.h>
#include <clBLAS.h>

#include <common.h>
#include <clBLAS-wrapper.h>
#include <BlasBase.h>
#include <syr.h>
#include <blas-random.h>

#ifdef PERF_TEST_WITH_ACML
#include <blas-internal.h>
#include <blas-wrapper.h>
#endif

#include "PerformanceTest.h"

/*
 * NOTE: operation factor means overall number
 *       of multiply and add per each operation involving
 *       2 matrix elements
 */

using namespace std;
using namespace clMath;

#define CHECK_RESULT(ret)                                                   \
do {                                                                        \
    ASSERT_GE(ret, 0) << "Fatal error: can not allocate resources or "      \
                         "perform an OpenCL request!" << endl;              \
    EXPECT_EQ(0, ret) << "The OpenCL version is slower in the case" <<      \
                         endl;                                              \
} while (0)

namespace clMath {

template <typename ElemType> class SyrPerformanceTest : public PerformanceTest
{
public:
    virtual ~SyrPerformanceTest();

    virtual int prepare(void);
    virtual nano_time_t etalonPerfSingle(void);
    virtual nano_time_t clblasPerfSingle(void);

    static void runInstance(BlasFunction fn, TestParams *params)
    {
        SyrPerformanceTest<ElemType> perfCase(fn, params);
        int ret = 0;
        int opFactor;
        BlasBase *base;

        base = clMath::BlasBase::getInstance();

        opFactor = 1;

        if ((fn == FN_DSYR) &&
            !base->isDevSupportDoublePrecision()) {

            std::cerr << ">> WARNING: The target device doesn't support native "
                         "double precision floating point arithmetic" <<
                         std::endl << ">> Test skipped" << std::endl;
            return;
        }

        if (!perfCase.areResourcesSufficient(params)) {
            std::cerr << ">> RESOURCE CHECK: Skip due to unsufficient resources" <<
                        std::endl;
        }
        else {
            ret = perfCase.run(opFactor);
        }

        ASSERT_GE(ret, 0) << "Fatal error: can not allocate resources or "
                             "perform an OpenCL request!" << endl;
        EXPECT_EQ(0, ret) << "The OpenCL version is slower in the case" << endl;
    }

private:
    SyrPerformanceTest(BlasFunction fn, TestParams *params);

    bool areResourcesSufficient(TestParams *params);

    TestParams params_;
    ElemType alpha_;
    ElemType *A_;
    ElemType *X_;
	ElemType *backA_;
    cl_mem mobjA_;
    cl_mem mobjX_;
    ::clMath::BlasBase *base_;
};

template <typename ElemType>
SyrPerformanceTest<ElemType>::SyrPerformanceTest(
    BlasFunction fn,
    TestParams *params) : PerformanceTest(fn, (problem_size_t)((((params->N * params->N) + params->N) * 2 ) * sizeof(ElemType))),
                          params_(*params), mobjA_(NULL), mobjX_(NULL)
{
    A_ = new ElemType[params_.N * params_.lda + params_.offa];
    X_ = new ElemType[ 1 + (params_.N-1) * abs(params_.incx)  + params_.offBX];
    backA_ = new ElemType[params_.N * params_.lda + params_.offa];

    base_ = ::clMath::BlasBase::getInstance();
}

template <typename ElemType>
SyrPerformanceTest<ElemType>::~SyrPerformanceTest()
{
    if(A_ != NULL)
    {
    delete[] A_;
    }
	if(backA_ != NULL)
	{
		delete[] backA_;
	}
	if(X_ != NULL)
	{
    delete[] X_;
	}

	if(mobjX_ != NULL) {
		clReleaseMemObject(mobjX_);
    }
	if(mobjA_ != NULL) {
		clReleaseMemObject(mobjA_);
	}
}

/*
 * Check if available OpenCL resources are sufficient to
 * run the test case
 */
template <typename ElemType> bool
SyrPerformanceTest<ElemType>::areResourcesSufficient(TestParams *params)
{
    clMath::BlasBase *base;
    size_t gmemSize, allocSize;
    size_t n = params->N;

	if((A_ == NULL) || (backA_ == NULL) || (X_ == NULL))
	{
        return 0;
	}

    base = clMath::BlasBase::getInstance();
    gmemSize = (size_t)base->availGlobalMemSize( 0 );
    allocSize = (size_t)base->maxMemAllocSize();

	bool suff = ( sizeof(ElemType)*n*params->lda < allocSize ) && ((1 + (n-1)*abs(params->incx))*sizeof(ElemType) < allocSize); //for individual allocations
    suff = suff && ((( n*params->lda + (1 + (n-1)*abs(params->incx))*2)*sizeof(ElemType)) < gmemSize) ; //for total global allocations

    return suff ;
}

template <typename ElemType> int
SyrPerformanceTest<ElemType>::prepare(void)
{
    bool useAlpha = true;
	size_t lenX = 1 + (params_.N-1) * abs(params_.incx);

    alpha_ = convertMultiplier<ElemType>(params_.alpha);
/*
	int creationFlags = 0;
    creationFlags =  creationFlags | RANDOM_INIT;

    // Default is Column-Major

	creationFlags = ( (this-> params_.order) == clblasRowMajor)? (creationFlags | ROW_MAJOR_ORDER) : (creationFlags);
    creationFlags = ( (this-> params_.uplo) == clblasLower)? (creationFlags | LOWER_HALF_ONLY) : (creationFlags | UPPER_HALF_ONLY);
	BlasRoutineID BlasFn = CLBLAS_SYR;

    // Matrix A
    populate( (A_ + params_.offa), params_.N, params_.N, params_.lda, BlasFn, creationFlags);
    populate( X_ , lenX + params_.offBX, 1, lenX + params_.offBX, BlasFn);
    */

	randomSyrMatrices( params_.order, params_.uplo, params_.N, useAlpha, &alpha_, A_, params_.lda, X_, params_.incx);

	memcpy(backA_, A_, ((params_.N * params_.lda + params_.offa)* sizeof(ElemType)));

    mobjA_ = base_->createEnqueueBuffer(A_, (params_.N * params_.lda + params_.offa)* sizeof(*A_), 0, CL_MEM_READ_WRITE);
    mobjX_ = base_->createEnqueueBuffer(X_, (lenX + params_.offBX )* sizeof(*X_), 0, CL_MEM_READ_ONLY);

    return ( (mobjA_ != NULL) &&  (mobjX_ != NULL) ) ? 0 : -1;
}

template <typename ElemType> nano_time_t
SyrPerformanceTest<ElemType>::etalonPerfSingle(void)
{
    nano_time_t time = 0;
    clblasOrder order;
    clblasUplo fUplo;
	size_t lda;

#ifndef PERF_TEST_WITH_ROW_MAJOR
    if (params_.order == clblasRowMajor) {
        cerr << "Row major order is not allowed" << endl;
        return NANOTIME_ERR;
    }
#endif

    order = params_.order;
    lda = params_.lda;
    fUplo = params_.uplo;

#ifdef PERF_TEST_WITH_ACML

	if (order != clblasColumnMajor)
    {
        order = clblasColumnMajor;
        fUplo =  (params_.uplo == clblasUpper)? clblasLower : clblasUpper;
    }

   	time = getCurrentTime();
   	clMath::blas::syr(order, fUplo, params_.N, alpha_, X_, params_.offBX, params_.incx, A_, params_.offa, lda);
	time = getCurrentTime() - time;

#endif  // PERF_TEST_WITH_ACML

    return time;
}


template <typename ElemType> nano_time_t
SyrPerformanceTest<ElemType>::clblasPerfSingle(void)
{
    nano_time_t time;
    cl_event event;
    cl_int status;
    cl_command_queue queue = base_->commandQueues()[0];

    status = clEnqueueWriteBuffer(queue, mobjA_, CL_TRUE, 0,
                                  ((params_.N * params_.lda) + params_.offa) *
                                  sizeof(ElemType), backA_, 0, NULL, &event);
    if (status != CL_SUCCESS) {
        cerr << "Matrix A buffer object enqueuing error, status = " <<
                 status << endl;

        return NANOTIME_ERR;
    }

    status = clWaitForEvents(1, &event);
    if (status != CL_SUCCESS) {
        cout << "Wait on event failed, status = " <<
                status << endl;

        return NANOTIME_ERR;
    }

    event = NULL;

#define TIMING
#ifdef TIMING
    clFinish( queue);
    time = getCurrentTime();

    int iter = 100;
    for ( int i = 1; i <= iter; i++)
    {
#endif
    status = (cl_int)clMath::clblas::syr(params_.order, params_.uplo, params_.N, alpha_, mobjX_, params_.offBX, params_.incx,
				mobjA_, params_.offa, params_.lda, 1, &queue, 0, NULL, &event);

    if (status != CL_SUCCESS) {
        cerr << "The CLBLAS SYR function failed, status = " <<
                status << endl;

        return NANOTIME_ERR;
    }

#ifdef TIMING
    } // iter loop
    clFinish( queue);
    time = getCurrentTime() - time;
    time /= iter;
#else
    status = flushAll(1, &queue);
    if (status != CL_SUCCESS) {
        cerr << "clFlush() failed, status = " << status << endl;
        return NANOTIME_ERR;
    }

    time = getCurrentTime();
    status = waitForSuccessfulFinish(1, &queue, &event);
    if (status == CL_SUCCESS) {
        time = getCurrentTime() - time;
    }
    else {
        cerr << "Waiting for completion of commands to the queue failed, "
                "status = " << status << endl;
        time = NANOTIME_ERR;
    }
#endif

    return time;
}

} // namespace clMath

// ssyr performance test
TEST_P(SYR, ssyr)
{
    TestParams params;

    getParams(&params);
    SyrPerformanceTest<float>::runInstance(FN_SSYR, &params);
}

// dsyr performance test case
TEST_P(SYR, dsyr)
{
    TestParams params;

    getParams(&params);
    SyrPerformanceTest<double>::runInstance(FN_DSYR, &params);
}
