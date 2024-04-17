/*
 * Copyright (c) The acados authors.
 *
 * This file is part of acados.
 *
 * The 2-Clause BSD License
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.;
 */


#include <assert.h>
#include <stdlib.h>

#include "acados/utils/external_function_generic.h"
#include "acados/utils/mem.h"

/************************************************
 * generic external function
 ************************************************/



/************************************************
 * generic external parametric function
 ************************************************/

acados_size_t external_function_param_generic_struct_size()
{
    return sizeof(external_function_param_generic);
}



void external_function_param_generic_set_fun(external_function_param_generic *fun, void *value)
{
    fun->fun = value;
    return;
}

static void external_function_param_generic_set_param_sparse(void *self, int n_update,
                                                             int *idx, double *p)
{
    external_function_param_generic *fun = self;

    for (int ii = 0; ii < n_update; ii++)
    {
        fun->p[idx[ii]] = p[ii];
    }

    return;
}


acados_size_t external_function_param_generic_calculate_size(external_function_param_generic *fun, int np)
{
    // wrapper as evaluate function
    fun->evaluate = &external_function_param_generic_wrapper;

    // set param function
    fun->get_nparam = &external_function_param_generic_get_nparam;
    fun->set_param = &external_function_param_generic_set_param;
    fun->set_param_sparse = &external_function_param_generic_set_param_sparse;

    // set number of parameters
    fun->np = np;

    acados_size_t size = 0;

    // doubles
    size += fun->np * sizeof(double); // p

    size += 8;  // align to double

    make_int_multiple_of(8, &size);

    return size;
}



void external_function_param_generic_assign(external_function_param_generic *fun, void *raw_memory)
{
    // save initial pointer to external memory
    fun->ptr_ext_mem = raw_memory;

    // char pointer for byte advances
    char *c_ptr = raw_memory;

    // align to double
    align_char_to(8, &c_ptr);

    // p
    assign_and_advance_double(fun->np, &fun->p, &c_ptr);

    assert((char *) raw_memory + external_function_param_generic_calculate_size(fun, fun->np) >= c_ptr);

    return;
}



void external_function_param_generic_wrapper(void *self, ext_fun_arg_t *type_in, void **in, ext_fun_arg_t *type_out, void **out)
{
    // cast into external generic function
    external_function_param_generic *fun = self;

    // call casadi function
    fun->fun(in, out, fun->p);

    return;
}



void external_function_param_generic_get_nparam(void *self, int *np)
{
    // cast into external generic function
    external_function_param_generic *fun = self;

	*np = fun->np;

    return;
}



void external_function_param_generic_set_param(void *self, double *p)
{
    // cast into external generic function
    external_function_param_generic *fun = self;

    // set value for all parameters
    for (int ii = 0; ii < fun->np; ii++)
    {
        fun->p[ii] = p[ii];
    }

    return;
}


/************************************************
 * casadi utils
 ************************************************/

static int casadi_nnz(const int *sparsity)
{
    int nnz = 0;
    if (sparsity != NULL)
    {
        const int nrow = sparsity[0];
        const int ncol = sparsity[1];
        const int dense = sparsity[2];
        if (dense)
        {
            nnz = nrow * ncol;
        }
        else
        {
            const int *idxcol = sparsity + 2;
            for (int i = 0; i < ncol; ++i)
            {
                nnz += idxcol[i + 1] - idxcol[i];
            }
        }
    }

    return nnz;
}


static int casadi_is_dense(const int *sparsity)
{
    // returns true if casadi sparsity goes through all elements of the matrix as it would for a dense matrix;
    // otherwise false.
    int idx, jj;
    if (sparsity != NULL)
    {
        const int nrow = sparsity[0];
        const int ncol = sparsity[1];
        const int dense = sparsity[2];
        if ((nrow<=0 )| (ncol<=0))
            return 1;
        if (dense)
        {
            return 1;
        }
        else
        {
            // check for casadi fake sparse
            const int *idxcol = sparsity + 2;
            const int *row = sparsity + ncol + 3;
            for (jj = 0; jj < ncol; jj++)
            {
                if (idxcol[jj + 1] - idxcol[jj] != nrow)
                {
                    // printf("casadi_is_dense: REAL sparse!\n");
                    // printf("idxco[jj+1] = %d idxcol[jj] %d nrow %d\n", idxcol[jj + 1], idxcol[jj], nrow);
                    return 0;
                }
                for (idx = idxcol[jj]; idx != idxcol[jj + 1]; idx++)
                {
                    if (row[idx]+jj*ncol != idx)
                    {
                        // printf("casadi_is_dense: REAL sparse!\n");
                        // printf("row[idx] %d != idx = %d, nrow %d ncol %d, jj %d\n", row[idx], idx, nrow, ncol, jj);
                        return 0;
                    }
                }
            }
        }
    }
    // printf("casadi_is_dense: real DENSE!\n");

    return 1;
}





static void d_cvt_casadi_to_colmaj(double *in, int *sparsity_in, double *out, int is_dense)
{
    int ii, jj, idx;

    int nrow = sparsity_in[0];
    int ncol = sparsity_in[1];

    if ((nrow<=0 )| (ncol<=0))
        return;

    if (is_dense)
    {
        // printf("d_cvt_casadi_to_colmaj: dense\n");
        for (ii = 0; ii < ncol * nrow; ii++) out[ii] = in[ii];
    }
    else
    {
        // printf("d_cvt_casadi_to_colmaj: sparse\n");
        double *ptr = in;
        int *idxcol = sparsity_in + 2;
        int *row = sparsity_in + ncol + 3;
        // Fill with zeros
        for (jj = 0; jj < ncol; jj++)
            for (ii = 0; ii < nrow; ii++) out[ii + jj * nrow] = 0.0;
        // Copy nonzeros
        for (jj = 0; jj < ncol; jj++)
        {
            for (idx = idxcol[jj]; idx != idxcol[jj + 1]; idx++)
            {
                out[row[idx] + jj * nrow] = ptr[0];
                ptr++;
            }
        }
    }

    return;
}



static void d_cvt_colmaj_to_casadi(double *in, double *out, int *sparsity_out, int is_dense)
{
    int ii, jj, idx;

    int nrow = sparsity_out[0];
    int ncol = sparsity_out[1];

    if ((nrow<=0 )| (ncol<=0))
        return;

    if (is_dense)
    {
        // printf("d_cvt_colmaj_to_casadi: dense\n");
        for (ii = 0; ii < ncol * nrow; ii++) out[ii] = in[ii];
    }
    else
    {
        // printf("d_cvt_colmaj_to_casadi: sparse\n");
        double *ptr = out;
        int *idxcol = sparsity_out + 2;
        int *row = sparsity_out + ncol + 3;
        // Copy nonzeros
        for (jj = 0; jj < ncol; jj++)
        {
            for (idx = idxcol[jj]; idx != idxcol[jj + 1]; idx++)
            {
                ptr[0] = in[row[idx] + jj * nrow];
                ptr++;
            }
        }
    }

    return;
}



static void d_cvt_casadi_to_dmat(double *in, int *sparsity_in, struct blasfeo_dmat *out, int is_dense)
{
    int jj, idx;

    int nrow = sparsity_in[0];
    int ncol = sparsity_in[1];

    if ((nrow<=0 )| (ncol<=0))
        return;

    if (is_dense)
    {
        blasfeo_pack_dmat(nrow, ncol, in, nrow, out, 0, 0);
    }
    else
    {
        double *ptr = in;
        int *idxcol = sparsity_in + 2;
        int *row = sparsity_in + ncol + 3;
        // Fill with zeros
        blasfeo_dgese(nrow, ncol, 0.0, out, 0, 0);
        // Copy nonzeros
        for (jj = 0; jj < ncol; jj++)
        {
            for (idx = idxcol[jj]; idx != idxcol[jj + 1]; idx++)
            {
                BLASFEO_DMATEL(out, row[idx], jj) = ptr[0];
                ptr++;
            }
        }
    }

    return;
}



static void d_cvt_dmat_to_casadi(struct blasfeo_dmat *in, double *out, int *sparsity_out, int is_dense)
{
    int jj, idx;

    int nrow = sparsity_out[0];
    int ncol = sparsity_out[1];

    if ((nrow<=0 )| (ncol<=0))
        return;

    if (is_dense)
    {
        blasfeo_unpack_dmat(nrow, ncol, in, 0, 0, out, nrow);
    }
    else
    {
        double *ptr = out;
        int *idxcol = sparsity_out + 2;
        int *row = sparsity_out + ncol + 3;
        // Copy nonzeros
        for (jj = 0; jj < ncol; jj++)
        {
            for (idx = idxcol[jj]; idx != idxcol[jj + 1]; idx++)
            {
                ptr[0] = BLASFEO_DMATEL(in, row[idx], jj);
                ptr++;
            }
        }
    }

    return;
}



// column vector: assume sparsity_in[1] = 1 !!! or empty vector;
static void d_cvt_casadi_to_dvec(double *in, int *sparsity_in, struct blasfeo_dvec *out, int is_dense)
{
    int idx;

    assert((sparsity_in[1] == 1) | (sparsity_in[0] == 0) | (sparsity_in[1] == 0));

    int n = sparsity_in[0];

    if (n<=0)
        return;

    if (is_dense)
    {
        blasfeo_pack_dvec(n, in, 1, out, 0);
    }
    else
    {
        double *ptr = in;
        int *idxcol = sparsity_in + 2;
        int *row = sparsity_in + 1 + 3;
        // Fill with zeros
        blasfeo_dvecse(n, 0.0, out, 0);
        // Copy nonzeros
        for (idx = idxcol[0]; idx != idxcol[1]; idx++)
        {
            BLASFEO_DVECEL(out, row[idx]) = ptr[0];
            ptr++;
        }
    }

    return;
}



// column vector: assume sparsity_in[1] = 1 !!! or empty vector;
static void d_cvt_dvec_to_casadi(struct blasfeo_dvec *in, double *out, int *sparsity_out, int is_dense)
{
    int idx;

    assert((sparsity_out[1] == 1) | (sparsity_out[0] == 0) | (sparsity_out[1] == 0));

    int n = sparsity_out[0];

    if (n<=0)
        return;

    if (is_dense)
    {
        blasfeo_unpack_dvec(n, in, 0, out, 1);
    }
    else
    {
        double *ptr = out;
        int *idxcol = sparsity_out + 2;
        int *row = sparsity_out + 1 + 3;
        // Copy nonzeros
        for (idx = idxcol[0]; idx != idxcol[1]; idx++)
        {
            ptr[0] = BLASFEO_DVECEL(in, row[idx]);
            ptr++;
        }
    }

    return;
}



static void d_cvt_casadi_to_colmaj_args(double *in, int *sparsity_in, struct colmaj_args *out, int is_dense)
{
    int ii, jj, idx;

    int nrow = sparsity_in[0];
    int ncol = sparsity_in[1];

    if ((nrow<=0 )| (ncol<=0))
        return;

    double *A = out->A;
    int lda = out->lda;

    if (is_dense)
    {
        for (ii = 0; ii < ncol; ii++)
            for (jj = 0; jj < nrow; jj++) A[ii + jj * lda] = in[ii + ncol * jj];
    }
    else
    {
        double *ptr = in;
        int *idxcol = sparsity_in + 2;
        int *row = sparsity_in + ncol + 3;
        // Fill with zeros
        for (jj = 0; jj < ncol; jj++)
            for (ii = 0; ii < nrow; ii++) A[ii + jj * lda] = 0.0;
        // Copy nonzeros
        for (jj = 0; jj < ncol; jj++)
        {
            for (idx = idxcol[jj]; idx != idxcol[jj + 1]; idx++)
            {
                A[row[idx] + jj * lda] = ptr[0];
                ptr++;
            }
        }
    }

    return;
}



static void d_cvt_colmaj_args_to_casadi(struct colmaj_args *in, double *out, int *sparsity_out, int is_dense)
{
    int ii, jj, idx;

    int nrow = sparsity_out[0];
    int ncol = sparsity_out[1];

    if ((nrow<=0 )| (ncol<=0))
        return;

    double *A = in->A;
    int lda = in->lda;

    if (is_dense)
    {
        for (ii = 0; ii < ncol; ii++)
            for (jj = 0; jj < nrow; jj++) out[ii + ncol * jj] = A[ii + jj * lda];
    }
    else
    {
        double *ptr = out;
        int *idxcol = sparsity_out + 2;
        int *row = sparsity_out + ncol + 3;
        // Copy nonzeros
        for (jj = 0; jj < ncol; jj++)
        {
            for (idx = idxcol[jj]; idx != idxcol[jj + 1]; idx++)
            {
                ptr[0] = A[row[idx] + jj * lda];
                ptr++;
            }
        }
    }

    return;
}



static void d_cvt_casadi_to_dmat_args(double *in, int *sparsity_in, struct blasfeo_dmat_args *out, int is_dense)
{
    int jj, idx;

    int nrow = sparsity_in[0];
    int ncol = sparsity_in[1];

    if ((nrow<=0 )| (ncol<=0))
        return;

    struct blasfeo_dmat *A = out->A;
    int ai = out->ai;
    int aj = out->aj;

    if (is_dense)
    {
        blasfeo_pack_dmat(nrow, ncol, in, nrow, A, ai, aj);
    }
    else
    {
        double *ptr = in;
        int *idxcol = sparsity_in + 2;
        int *row = sparsity_in + ncol + 3;
        // Fill with zeros
        blasfeo_dgese(nrow, ncol, 0.0, A, ai, aj);
        // Copy nonzeros
        for (jj = 0; jj < ncol; jj++)
        {
            for (idx = idxcol[jj]; idx != idxcol[jj + 1]; idx++)
            {
                BLASFEO_DMATEL(A, ai + row[idx], aj + jj) = ptr[0];
                ptr++;
            }
        }
    }

    return;
}



static void d_cvt_dmat_args_to_casadi(struct blasfeo_dmat_args *in, double *out, int *sparsity_out, int is_dense)
{
    int jj, idx;

    int nrow = sparsity_out[0];
    int ncol = sparsity_out[1];

    if ((nrow<=0 )| (ncol<=0))
        return;

    struct blasfeo_dmat *A = in->A;
    int ai = in->ai;
    int aj = in->aj;

    if (is_dense)
    {
        blasfeo_unpack_dmat(nrow, ncol, A, ai, aj, out, nrow);
    }
    else
    {
        double *ptr = out;
        int *idxcol = sparsity_out + 2;
        int *row = sparsity_out + ncol + 3;
        // Copy nonzeros
        for (jj = 0; jj < ncol; jj++)
        {
            for (idx = idxcol[jj]; idx != idxcol[jj + 1]; idx++)
            {
                ptr[0] = BLASFEO_DMATEL(A, ai + row[idx], aj + jj);
                ptr++;
            }
        }
    }

    return;
}



// column vector: assume sparsity_in[1] = 1 !!! or empty vector;
static void d_cvt_casadi_to_dvec_args(double *in, int *sparsity_in, struct blasfeo_dvec_args *out, int is_dense)
{
    int idx;

    assert((sparsity_in[1] == 1) | (sparsity_in[0] == 0) | (sparsity_in[1] == 0));

    int n = sparsity_in[0];

    if (n<=0)
        return;

    struct blasfeo_dvec *x = out->x;
    int xi = out->xi;

    if (is_dense)
    {
        blasfeo_pack_dvec(n, in, 1, x, xi);
    }
    else
    {
        double *ptr = in;
        int *idxcol = sparsity_in + 2;
        int *row = sparsity_in + 1 + 3;
        // Fill with zeros
        blasfeo_dvecse(n, 0.0, x, xi);
        // Copy nonzeros
        for (idx = idxcol[0]; idx != idxcol[1]; idx++)
        {
            BLASFEO_DVECEL(x, xi + row[idx]) = ptr[0];
            ptr++;
        }
    }

    return;
}



// column vector: assume sparsity_in[1] = 1 !!! or empty vector;
static void d_cvt_dvec_args_to_casadi(struct blasfeo_dvec_args *in, double *out, int *sparsity_out, int is_dense)
{
    int idx;

    assert((sparsity_out[1] == 1) | (sparsity_out[0] == 0) | (sparsity_out[1] == 0));

    int n = sparsity_out[0];

    if (n<=0)
        return;

    struct blasfeo_dvec *x = in->x;
    int xi = in->xi;

    if (is_dense)
    {
        blasfeo_unpack_dvec(n, x, xi, out, 1);
    }
    else
    {
        double *ptr = out;
        int *idxcol = sparsity_out + 2;
        int *row = sparsity_out + 1 + 3;
        // Copy nonzeros
        for (idx = idxcol[0]; idx != idxcol[1]; idx++)
        {
            ptr[0] = BLASFEO_DVECEL(x, xi + row[idx]);
            ptr++;
        }
    }

    return;
}



/************************************************
 * casadi external function
 ************************************************/

acados_size_t external_function_casadi_struct_size()
{
    return sizeof(external_function_casadi);
}



void external_function_casadi_set_fun(external_function_casadi *fun, void *value)
{
    fun->casadi_fun = value;
    return;
}



void external_function_casadi_set_work(external_function_casadi *fun, void *value)
{
    fun->casadi_work = value;
    return;
}



void external_function_casadi_set_sparsity_in(external_function_casadi *fun, void *value)
{
    fun->casadi_sparsity_in = value;
    return;
}



void external_function_casadi_set_sparsity_out(external_function_casadi *fun, void *value)
{
    fun->casadi_sparsity_out = value;
    return;
}



void external_function_casadi_set_n_in(external_function_casadi *fun, void *value)
{
    fun->casadi_n_in = value;
    return;
}



void external_function_casadi_set_n_out(external_function_casadi *fun, void *value)
{
    fun->casadi_n_out = value;
    return;
}



acados_size_t external_function_casadi_calculate_size(external_function_casadi *fun)
{
    // casadi wrapper as evaluate
    fun->evaluate = &external_function_casadi_wrapper;

    // loop index
    int ii;

    fun->casadi_work(&fun->args_num, &fun->res_num, &fun->iw_size, &fun->w_size);

    fun->in_num = fun->casadi_n_in();
    fun->out_num = fun->casadi_n_out();

    // args
    fun->args_size_tot = 0;
    for (ii = 0; ii < fun->args_num; ii++)
        fun->args_size_tot += casadi_nnz(fun->casadi_sparsity_in(ii));

    // res
    fun->res_size_tot = 0;
    for (ii = 0; ii < fun->res_num; ii++)
        fun->res_size_tot += casadi_nnz(fun->casadi_sparsity_out(ii));

    acados_size_t size = 0;

    // double pointers
    size += fun->args_num * sizeof(double *);  // args
    size += fun->res_num * sizeof(double *);   // res

    // ints
    size += 2 * fun->args_num * sizeof(int);  // args_size, args_dense
    size += 2 * fun->res_num * sizeof(int);   // res_size, res_dense
    size += fun->iw_size * sizeof(int);   // iw

    // doubles
    size += fun->args_size_tot * sizeof(double);  // args
    size += fun->res_size_tot * sizeof(double);   // res
    size += fun->w_size * sizeof(double);         // w

    size += 8;  // initial align
    size += 8;  // align to double

    make_int_multiple_of(8, &size);

    return size;
}



void external_function_casadi_assign(external_function_casadi *fun, void *raw_memory)
{
    // loop index
    int ii;

    // save initial pointer to external memory
    fun->ptr_ext_mem = raw_memory;

    // char pointer for byte advances
    char *c_ptr = raw_memory;

    // double pointers

    // initial align
    align_char_to(8, &c_ptr);

    // args
    assign_and_advance_double_ptrs(fun->args_num, &fun->args, &c_ptr);
    // res
    assign_and_advance_double_ptrs(fun->res_num, &fun->res, &c_ptr);

    // args_size, args_dense
    assign_and_advance_int(fun->args_num, &fun->args_size, &c_ptr);
    assign_and_advance_int(fun->args_num, &fun->args_dense, &c_ptr);
    for (ii = 0; ii < fun->args_num; ii++)
    {
        fun->args_size[ii] = casadi_nnz(fun->casadi_sparsity_in(ii));
        fun->args_dense[ii] = casadi_is_dense(fun->casadi_sparsity_in(ii));
    }
    // res_size, res_dense
    assign_and_advance_int(fun->res_num, &fun->res_size, &c_ptr);
    assign_and_advance_int(fun->res_num, &fun->res_dense, &c_ptr);
    for (ii = 0; ii < fun->res_num; ii++)
    {
        fun->res_size[ii] = casadi_nnz(fun->casadi_sparsity_out(ii));
        fun->res_dense[ii] = casadi_is_dense(fun->casadi_sparsity_out(ii));
    }
    // iw
    assign_and_advance_int(fun->iw_size, &fun->iw, &c_ptr);

    // align to double
    align_char_to(8, &c_ptr);

    // args
    for (ii = 0; ii < fun->args_num; ii++)
        assign_and_advance_double(fun->args_size[ii], &fun->args[ii], &c_ptr);
    // res
    for (ii = 0; ii < fun->res_num; ii++)
        assign_and_advance_double(fun->res_size[ii], &fun->res[ii], &c_ptr);
    // w
    assign_and_advance_double(fun->w_size, &fun->w, &c_ptr);

    assert((char *) raw_memory + external_function_casadi_calculate_size(fun) >= c_ptr);

    return;
}



void external_function_casadi_wrapper(void *self, ext_fun_arg_t *type_in, void **in,
                                      ext_fun_arg_t *type_out, void **out)
{
    // cast into external casadi function
    external_function_casadi *fun = self;

    // loop index
    int ii;

    // in as args
    for (ii = 0; ii < fun->in_num; ii++)
    {
        switch (type_in[ii])
        {
            case COLMAJ:
                d_cvt_colmaj_to_casadi(in[ii], (double *) fun->args[ii],
                                       (int *) fun->casadi_sparsity_in(ii),
                                       fun->args_dense[ii]);
                break;

            case BLASFEO_DMAT:
                d_cvt_dmat_to_casadi(in[ii], (double *) fun->args[ii],
                                     (int *) fun->casadi_sparsity_in(ii),
                                     fun->args_dense[ii]);
                break;

            case BLASFEO_DVEC:
                d_cvt_dvec_to_casadi(in[ii], (double *) fun->args[ii],
                                     (int *) fun->casadi_sparsity_in(ii),
                                     fun->args_dense[ii]);
                break;

            case COLMAJ_ARGS:
                d_cvt_colmaj_args_to_casadi(in[ii], (double *) fun->args[ii],
                                            (int *) fun->casadi_sparsity_in(ii),
                                            fun->args_dense[ii]);
                break;

            case BLASFEO_DMAT_ARGS:
                d_cvt_dmat_args_to_casadi(in[ii], (double *) fun->args[ii],
                                          (int *) fun->casadi_sparsity_in(ii),
                                          fun->args_dense[ii]);
                break;

            case BLASFEO_DVEC_ARGS:
                d_cvt_dvec_args_to_casadi(in[ii], (double *) fun->args[ii],
                                          (int *) fun->casadi_sparsity_in(ii),
                                          fun->args_dense[ii]);
                break;

            case IGNORE_ARGUMENT:
                // do nothing
                break;

            default:
                printf("\ntype in %d\n", type_in[ii]);
                printf("\nUnknown external function argument type for argument %i\n\n", ii);
                exit(1);
        }
    }

    // call casadi function
    fun->casadi_fun((const double **) fun->args, fun->res, fun->iw, fun->w, NULL);

    for (ii = 0; ii < fun->out_num; ii++)
    {
        switch (type_out[ii])
        {
            case COLMAJ:
                d_cvt_casadi_to_colmaj((double *) fun->res[ii],
                                       (int *) fun->casadi_sparsity_out(ii), out[ii],
                                       fun->res_dense[ii]);
                break;

            case BLASFEO_DMAT:
                d_cvt_casadi_to_dmat((double *) fun->res[ii], (int *) fun->casadi_sparsity_out(ii),
                                     out[ii],
                                     fun->res_dense[ii]);
                break;

            case BLASFEO_DVEC:
                d_cvt_casadi_to_dvec((double *) fun->res[ii], (int *) fun->casadi_sparsity_out(ii),
                                     out[ii],
                                     fun->res_dense[ii]);
                break;

            case COLMAJ_ARGS:
                d_cvt_casadi_to_colmaj_args((double *) fun->res[ii],
                                            (int *) fun->casadi_sparsity_out(ii), out[ii],
                                            fun->res_dense[ii]);
                break;

            case BLASFEO_DMAT_ARGS:
                d_cvt_casadi_to_dmat_args((double *) fun->res[ii],
                                          (int *) fun->casadi_sparsity_out(ii), out[ii],
                                          fun->res_dense[ii]);
                break;

            case BLASFEO_DVEC_ARGS:
                d_cvt_casadi_to_dvec_args((double *) fun->res[ii],
                                          (int *) fun->casadi_sparsity_out(ii), out[ii],
                                          fun->res_dense[ii]);
                break;

            case IGNORE_ARGUMENT:
                // do nothing
                break;

            default:
                printf("\ntype out %d\n", type_out[ii]);
                printf("\nUnknown external function argument type for output %i\n\n", ii);
                exit(1);
        }
    }

    return;
}

/************************************************
 * casadi external parametric function
 ************************************************/

acados_size_t external_function_param_casadi_struct_size()
{
    return sizeof(external_function_param_casadi);
}



void external_function_param_casadi_set_fun(external_function_param_casadi *fun, void *value)
{
    fun->casadi_fun = value;
    return;
}



void external_function_param_casadi_set_work(external_function_param_casadi *fun, void *value)
{
    fun->casadi_work = value;
    return;
}



void external_function_param_casadi_set_sparsity_in(external_function_param_casadi *fun, void *value)
{
    fun->casadi_sparsity_in = value;
    return;
}



void external_function_param_casadi_set_sparsity_out(external_function_param_casadi *fun, void *value)
{
    fun->casadi_sparsity_out = value;
    return;
}



void external_function_param_casadi_set_n_in(external_function_param_casadi *fun, void *value)
{
    fun->casadi_n_in = value;
    return;
}



void external_function_param_casadi_set_n_out(external_function_param_casadi *fun, void *value)
{
    fun->casadi_n_out = value;
    return;
}


static void external_function_param_casadi_set_param(void *self, double *p)
{
    external_function_param_casadi *fun = self;

    // set value for all parameters
    int idx_arg_p = fun->args_num-1;
    d_cvt_colmaj_to_casadi(p, (double *) fun->args[idx_arg_p],
                                       (int *) fun->casadi_sparsity_in(idx_arg_p), fun->args_dense[idx_arg_p]);
    return;
}


static void external_function_param_casadi_set_param_sparse(void *self, int n_update,
                                                            int *idx_p_update, double *p)
{
    // TODO: fix!!!
    // TODO: assert p is vector!!!
    external_function_param_casadi *fun = self;

    int idx_arg_p = fun->args_num-1;

    if (fun->args_dense[idx_arg_p])
    {
        for (int ii = 0; ii < n_update; ii++)
        {
            fun->args[idx_arg_p][idx_p_update[ii]] = p[ii];
        }
    }
    else
    {
        int *sparsity = fun->casadi_sparsity_in(idx_arg_p);
        int nrow = sparsity[0];
        int ncol = sparsity[1];
        double *ptr = fun->args[idx_arg_p];
        int *idxcol = sparsity + 2;
        int *row = sparsity + ncol + 3;

        int i_idx = 0;
        int ip;
        int nnz_p = casadi_nnz(sparsity);
        for (int ii = 0; ii < n_update; ii++)
        {
            ip = idx_p_update[ii];
            for (jj = 0; jj < nnz_p; j++)
            {
            }
            fun->args[idx_arg_p][] = p[ii];
        }
    }

    return;
}


acados_size_t external_function_param_casadi_calculate_size(external_function_param_casadi *fun, int np)
{
    // loop index
    int ii;

    // casadi wrapper as evaluate function
    fun->evaluate = &external_function_param_casadi_wrapper;

    // set param function
    fun->get_nparam = &external_function_param_casadi_get_nparam;
    fun->set_param = &external_function_param_casadi_set_param;
    fun->set_param_sparse = &external_function_param_casadi_set_param_sparse;

    // set number of parameters
    fun->np = np;

    fun->casadi_work(&fun->args_num, &fun->res_num, &fun->iw_size, &fun->w_size);

    fun->in_num = fun->casadi_n_in();
    fun->out_num = fun->casadi_n_out();

    // args
    fun->args_size_tot = 0;
    for (ii = 0; ii < fun->args_num; ii++)
        fun->args_size_tot += casadi_nnz(fun->casadi_sparsity_in(ii));

    // res
    fun->res_size_tot = 0;
    for (ii = 0; ii < fun->res_num; ii++)
        fun->res_size_tot += casadi_nnz(fun->casadi_sparsity_out(ii));

    acados_size_t size = 0;

    // double pointers
    size += fun->args_num * sizeof(double *);  // args
    size += fun->res_num * sizeof(double *);   // res

    // ints
    size += 2 * fun->args_num * sizeof(int);  // args_size, args_dense
    size += 2 * fun->res_num * sizeof(int);   // res_size, res_dense
    size += fun->iw_size * sizeof(int);   // iw

    // doubles
    size += fun->args_size_tot * sizeof(double);  // args
    size += fun->res_size_tot * sizeof(double);   // res
    size += fun->w_size * sizeof(double);         // w
    size += fun->np * sizeof(double);             // p

    size += 8;  // initial align
    size += 8;  // align to double

    make_int_multiple_of(8, &size);

    return size;
}



void external_function_param_casadi_assign(external_function_param_casadi *fun, void *raw_memory)
{
    // loop index
    int ii;

    // save initial pointer to external memory
    fun->ptr_ext_mem = raw_memory;

    // char pointer for byte advances
    char *c_ptr = raw_memory;

    // double pointers

    // initial align
    align_char_to(8, &c_ptr);

    // args
    assign_and_advance_double_ptrs(fun->args_num, &fun->args, &c_ptr);
    // res
    assign_and_advance_double_ptrs(fun->res_num, &fun->res, &c_ptr);

    // args_size, args_dense
    assign_and_advance_int(fun->args_num, &fun->args_size, &c_ptr);
    assign_and_advance_int(fun->args_num, &fun->args_dense, &c_ptr);
    for (ii = 0; ii < fun->args_num; ii++)
    {
        fun->args_size[ii] = casadi_nnz(fun->casadi_sparsity_in(ii));
        fun->args_dense[ii] = casadi_is_dense(fun->casadi_sparsity_in(ii));
    }
    // res_size, res_dense
    assign_and_advance_int(fun->res_num, &fun->res_size, &c_ptr);
    assign_and_advance_int(fun->res_num, &fun->res_dense, &c_ptr);
    for (ii = 0; ii < fun->res_num; ii++)
    {
        fun->res_size[ii] = casadi_nnz(fun->casadi_sparsity_out(ii));
        fun->res_dense[ii] = casadi_is_dense(fun->casadi_sparsity_out(ii));
    }
    // iw
    assign_and_advance_int(fun->iw_size, &fun->iw, &c_ptr);

    // align to double
    align_char_to(8, &c_ptr);

    // args
    for (ii = 0; ii < fun->args_num; ii++)
        assign_and_advance_double(fun->args_size[ii], &fun->args[ii], &c_ptr);
    // res
    for (ii = 0; ii < fun->res_num; ii++)
        assign_and_advance_double(fun->res_size[ii], &fun->res[ii], &c_ptr);
    // w
    assign_and_advance_double(fun->w_size, &fun->w, &c_ptr);

    assert((char *) raw_memory + external_function_param_casadi_calculate_size(fun, fun->np) >=
           c_ptr);

    return;
}



void external_function_param_casadi_wrapper(void *self, ext_fun_arg_t *type_in, void **in,
                                            ext_fun_arg_t *type_out, void **out)
{
    // cast into external casadi function
    external_function_param_casadi *fun = self;

    // loop index
    int ii;

    // in as args
    // skip last argument (that is the parameters vector)
    for (ii = 0; ii < fun->in_num - 1; ii++)
    {
        switch (type_in[ii])
        {
            case COLMAJ:
                d_cvt_colmaj_to_casadi(in[ii], (double *) fun->args[ii],
                                       (int *) fun->casadi_sparsity_in(ii), fun->args_dense[ii]);
                break;

            case BLASFEO_DMAT:
                d_cvt_dmat_to_casadi(in[ii], (double *) fun->args[ii],
                                     (int *) fun->casadi_sparsity_in(ii), fun->args_dense[ii]);
                break;

            case BLASFEO_DVEC:
                d_cvt_dvec_to_casadi(in[ii], (double *) fun->args[ii],
                                     (int *) fun->casadi_sparsity_in(ii), fun->args_dense[ii]);
                break;
            case COLMAJ_ARGS:
                d_cvt_colmaj_args_to_casadi(in[ii], (double *) fun->args[ii],
                                            (int *) fun->casadi_sparsity_in(ii), fun->args_dense[ii]);
                break;

            case BLASFEO_DMAT_ARGS:
                d_cvt_dmat_args_to_casadi(in[ii], (double *) fun->args[ii],
                                          (int *) fun->casadi_sparsity_in(ii), fun->args_dense[ii]);
                break;

            case BLASFEO_DVEC_ARGS:
                d_cvt_dvec_args_to_casadi(in[ii], (double *) fun->args[ii],
                                          (int *) fun->casadi_sparsity_in(ii), fun->args_dense[ii]);
                break;

            case IGNORE_ARGUMENT:
                // do nothing
                break;

            default:
                printf("\nexternal_function_param_casadi_wrapper: Unknown external function argument type %d for input %d\n\n", type_in[ii], ii);
                exit(1);
        }
    }
    // parameters are last argument and set via external_function_param_casadi_set_param

    // call casadi function
    fun->casadi_fun((const double **) fun->args, fun->res, fun->iw, fun->w, NULL);

    for (ii = 0; ii < fun->out_num; ii++)
    {
        switch (type_out[ii])
        {
            case COLMAJ:
                d_cvt_casadi_to_colmaj((double *) fun->res[ii],
                                       (int *) fun->casadi_sparsity_out(ii), out[ii],
                                       fun->res_dense[ii]);
                break;

            case BLASFEO_DMAT:
                d_cvt_casadi_to_dmat((double *) fun->res[ii], (int *) fun->casadi_sparsity_out(ii),
                                     out[ii], fun->res_dense[ii]);
                break;

            case BLASFEO_DVEC:
                d_cvt_casadi_to_dvec((double *) fun->res[ii], (int *) fun->casadi_sparsity_out(ii),
                                     out[ii], fun->res_dense[ii]);
                break;
            case COLMAJ_ARGS:
                d_cvt_casadi_to_colmaj_args((double *) fun->res[ii],
                                            (int *) fun->casadi_sparsity_out(ii), out[ii], fun->res_dense[ii]);
                break;

            case BLASFEO_DMAT_ARGS:
                d_cvt_casadi_to_dmat_args((double *) fun->res[ii],
                                          (int *) fun->casadi_sparsity_out(ii), out[ii], fun->res_dense[ii]);
                break;

            case BLASFEO_DVEC_ARGS:
                d_cvt_casadi_to_dvec_args((double *) fun->res[ii],
                                          (int *) fun->casadi_sparsity_out(ii), out[ii], fun->res_dense[ii]);
                break;

            case IGNORE_ARGUMENT:
                // do nothing
                break;

            default:
                printf("\nexternal_function_param_casadi_wrapper: Unknown external function argument type %d for output %d\n\n", type_out[ii], ii);
                exit(1);
        }
    }

    return;
}



void external_function_param_casadi_get_nparam(void *self, int *np)
{
    // cast into external casadi function
    external_function_param_casadi *fun = self;

	*np = fun->np;

    return;
}


