#include "common.h"


/**
 * Load an array of sample data.
 *   @path: The path.
 *   @arr: Ref. The output array.
 *   @len: The length.
 */
void snd_load(const char *path, double **arr, unsigned int *len)
{
	SNDFILE *file;
	SF_INFO info;

	info.format = 0;
	file = sf_open(path, SFM_READ, &info);
	if(file == NULL)
		fatal("Cannot open file '%s'.", path);

	if(info.channels > 1)
		fatal("File '%s' has more than 1 channel.", path);

	*arr = malloc(info.frames * sizeof(double));
	*len = info.frames;

	sf_readf_double(file, *arr, *len);
	sf_close(file);
}


/*
 * (x[n] - 2x[n-1] + x[n-2]) h^2 - K sin(x[n]) = 0
 */
void test1(void)
{
	double *in, *ref, *cmp;
	unsigned int i, k, len;

	snd_load("sample.flac", &in, &len);
	ref = malloc(len * sizeof(double));
	cmp = malloc(len * sizeof(double));

	//memcpy(in, (double[]){ 0,3, 5, 6}, 4*sizeof(double));
	//len = 4;

	ref[0] = 1.6*in[0];
	for(i = 1; i < len; i++)
		ref[i] = 1.6 * in[i] + ref[i-1];

	//printf("ref: %f %f %f %f\n", ref[0], ref[1], ref[2], ref[3]);

	struct fl_gen_t *gen;
	struct m_rand_t rand = m_rand_init(0);
	struct fl_weight_t weight = {
		.add = 8.0f,
		.sub = 2.0f,
		.mul = 8.0f,
		.div = 1.0f
	};

	fl_weight_norm(&weight);

	gen = fl_gen_new();
	fl_gen_const(gen, 1.6);
	fl_gen_add(gen, fl_inst_new(fl_func_new(1, 1, 1)));

	double s[1];

	for(k = 0; k < 1000000; k++) {
		struct fl_inst_t *inst;
		double diff, max = 0.0;

		inst = fl_gen_trial(gen, &weight, &rand);
		if(inst == NULL)
			continue;

		s[0] = 0.0;
		for(i = 0; i < len; i++) {
			fl_func_eval(inst->func, &in[i], &cmp[i], s);
			if(isnan(cmp[i]))
				break;

			diff = fabs(cmp[i] - ref[i]);
			max = fmax(diff, max);
			if(max > 0.001)
				break;
		}

		if(i == len) {
			printf("match: %g\n", max);
			//printf("HERE! %g : %f %f %f %f\n", max, cmp[0], cmp[1], cmp[2], cmp[3]);
			fl_func_dump(inst->func);
		}
	}

	fl_gen_delete(gen);

	free(in);
	free(ref);
	free(cmp);
}

/*
void r_sys_gather(struct r_sys_t *sys, struct r_var_t **vars)
{
}
*/


/*
 * local declarations
 */
static void gather_sys(struct rvec_var_t *var, struct r_sys_t *sys);
static void gather_rel(struct rvec_var_t *var, struct r_rel_t *rel);
static void gather_expr(struct rvec_var_t *var, struct r_expr_t *expr);


/**
 * Gather a vector of variables from a system of equations.
 *   @sys: The system of equations.
 *   &returns: The variable vector.
 */
struct rvec_var_t *rvec_gather_sys(struct r_sys_t *sys)
{
	struct rvec_var_t *var;

	var = rvec_var_new();
	gather_sys(var, sys);

	return var;
}


/**
 * Gather variables from a system of equations.
 *   @var: The variable vector.
 *   @sys: The system.
 */
static void gather_sys(struct rvec_var_t *var, struct r_sys_t *sys)
{
	while(sys != NULL) {
		gather_rel(var, sys->rel);
		sys = sys->next;
	}
}

/**
 * Gather variables from a relation.
 *   @var: The variable vector.
 *   @rel: The relation.
 */
static void gather_rel(struct rvec_var_t *var, struct r_rel_t *rel)
{
	gather_expr(var, rel->left);
	gather_expr(var, rel->right);
}

/**
 * Gather variables from an expression.
 *   @var: The variable vector.
 *   @expr: The expression.
 */
static void gather_expr(struct rvec_var_t *var, struct r_expr_t *expr)
{
	switch(expr->type) {
	case r_unk_v:
	case r_flt_v:
	case r_num_v:
	case r_const_v:
		break;

	case r_var_v:
		if(rvec_var_idx(var, expr->data.var) < 0)
			rvec_var_add(var, r_var_copy(expr->data.var));

		break;

	case r_neg_v:
		gather_expr(var, expr->data.expr);
		break;

	case r_add_v:
	case r_sub_v:
	case r_mul_v:
	case r_div_v:
		gather_expr(var, expr->data.op2.left);
		gather_expr(var, expr->data.op2.right);
		break;

	case r_sum_v:
		{
			struct r_list_t *list;

			for(list = expr->data.list; list != NULL; list = list->next)
				gather_expr(var, list->expr);
		}
		break;
	}
}


struct r_expr_t *r_deriv_expr(struct r_expr_t *expr, struct r_var_t *var);

struct r_expr_t *r_const_expr(struct r_expr_t *expr);


/**
 * Compute the derivative of an expression.
 *   @expr: The expression.
 *   @var: The independent variable.
 *   &returns: The derivative.
 */
struct r_expr_t *r_deriv_expr(struct r_expr_t *expr, struct r_var_t *var)
{
	switch(expr->type) {
	case r_unk_v:
	case r_flt_v:
	case r_num_v:
	case r_const_v:
		return r_expr_zero();

	case r_var_v:
		return r_expr_flt((expr->data.var == var) ? 1.0 : 0.0);

	case r_neg_v:
		return r_expr_neg(r_deriv_expr(expr->data.expr, var));

	case r_add_v:
	case r_sub_v:
		{
			struct r_expr_t *left, *right;

			left = r_deriv_expr(expr->data.op2.left, var);
			right = r_deriv_expr(expr->data.op2.right, var);

			switch(expr->type) {
			case r_add_v: return r_expr_add(left, right);
			case r_sub_v: return r_expr_sub(left, right);
			default: __builtin_unreachable();
			}
		}

	case r_mul_v:
		{
			struct r_expr_t *left, *right, *res[2];

			left = expr->data.op2.left;
			right = expr->data.op2.right;

			res[0] = r_expr_mul(r_expr_copy(left), r_deriv_expr(right, var));
			res[1] = r_expr_mul(r_deriv_expr(left, var), r_expr_copy(right));

			return r_expr_add(res[0], res[1]);
		}

	case r_div_v:
		fatal("stub");

	case r_sum_v:
		{
			struct r_list_t *list, *res, **iter;

			res = r_list_new();
			iter = &res;

			for(list = expr->data.list; list != NULL; list = list->next)
				iter = r_list_add(iter, r_deriv_expr(list->expr, var));

			switch(expr->type) {
			case r_sum_v: return r_expr_sum(res);
			default: __builtin_unreachable();
			}
		}
	}

	__builtin_unreachable();
}

/**
 * Compute the constant from an expression.
 *   @expr: The expression.
 *   &returns: The constant.
 */
struct r_expr_t *r_const_expr(struct r_expr_t *expr)
{
	switch(expr->type) {
	case r_unk_v:
	case r_flt_v:
	case r_num_v:
	case r_const_v:
		return r_expr_copy(expr);

	case r_var_v:
		return r_expr_zero();

	case r_neg_v:
		return r_expr_neg(r_const_expr(expr->data.expr));

	case r_add_v:
	case r_sub_v:
	case r_mul_v:
	case r_div_v:
		{
			struct r_expr_t *left, *right;

			left = r_const_expr(expr->data.op2.left);
			right = r_const_expr(expr->data.op2.right);

			switch(expr->type) {
			case r_add_v: return r_expr_add(left, right);
			case r_sub_v: return r_expr_sub(left, right);
			case r_mul_v: return r_expr_mul(left, right);
			case r_div_v: return r_expr_div(left, right);
			default: __builtin_unreachable();
			}
		}

	case r_sum_v:
		{
			struct r_list_t *list, *res, **iter;

			res = r_list_new();
			iter = &res;

			for(list = expr->data.list; list != NULL; list = list->next)
				iter = r_list_add(iter, r_const_expr(list->expr));

			switch(expr->type) {
			case r_sum_v: return r_expr_sum(res);
			default: __builtin_unreachable();
			}
		}
	}

	__builtin_unreachable();
}


void test2(void)
{
	struct cir_node_t *in, *out, *gnd, *res1, *res2;

	in = cir_node_input(mprintf("In"));
	out = cir_node_output(mprintf("Out"));
	gnd = cir_node_gnd();
	res1 = cir_node_res(100.0);
	res2 = cir_node_res(200.0);

	cir_connect(&in->port[0], &res1->port[0]);
	cir_connect(&res1->port[1], &res2->port[0]);
	cir_connect(&res1->port[1], &out->port[0]);
	cir_connect(&res2->port[1], &gnd->port[0]);

	{
		struct r_sys_t *sys, *iter;
		struct rvec_var_t *var;
		unsigned int i, j;

		sys = cir_system(in);
		var = rvec_gather_sys(sys);

		r_sys_norm(sys);
		r_sys_print(sys, io_file_wrap(stdout));

		if(var->len != r_sys_cnt(sys))
			fatal("Invalid system of equations: %d variables for %d equations.", var->len, r_sys_cnt(sys));

		struct rmat_expr_t *mat;
		struct rvec_expr_t *vec, *res;

		vec = rvec_expr_new(var->len);
		mat = rmat_expr_new(var->len, var->len);

		printf("%u::%u\n", var->len, r_sys_cnt(sys));
		for(iter = sys, j = 0; iter != NULL; iter = iter->next, j++) {
			for(i = 0; i < var->len; i++)
				r_expr_set(rmat_expr_get(mat, j, i), r_fold_expr_clr(r_deriv_expr(iter->rel->left, var->arr[i])));

			r_expr_set(&vec->arr[j], r_fold_expr_clr(r_expr_neg(r_const_expr(iter->rel->left))));
		}

		rmat_expr_dump(mat); printf("\n");

		struct rmat_expr_t *inv;

		inv = rmat_expr_inv(mat);
		inv = rmat_fold_expr_clr(inv);
		rmat_expr_dump(inv); printf("\n");
		//printf("%C\n", r_expr_chunk(inv));

		rvec_expr_dump(vec); printf("\n");

		printf(":: %d %d\n", inv->width, inv->height);
		res = rvec_fold_expr_clr(rvec_expr_mul(inv, vec));

		for(i = 0; i < res->len; i++) {
			printf("%s: %C\n", var->arr[i]->id, r_expr_chunk(res->arr[i]));
		}


		rmat_expr_delete(mat);
		rmat_expr_delete(inv);
		rvec_expr_delete(vec);
		rvec_expr_delete(res);
		rvec_var_delete(var);
		r_sys_delete(sys);
	}

	cir_node_delete(in);
	cir_node_delete(out);
	cir_node_delete(gnd);
	cir_node_delete(res1);
	cir_node_delete(res2);
}


/**
 * Main entry point.
 *   @argc: The number of argument.
 *   @argv: The argument array.
 *   &returns: Always zero.
 */
int main(int argc, char **argv)
{
	test2();

	/*
	struct cir_node_t *in, *out;

	in = cir_node_input(mprintf("In"));
	out = cir_node_output(mprintf("Out"));

	cir_connect(&in->port[0], &out->port[0]);

	{
		struct avltree_t tree;
		
		tree = cir_nodes(in);
		avltree_destroy(&tree);
	}

	cir_node_delete(in);
	cir_node_delete(out);
	*/

	/*
	struct fl_func_t *func;

	func = fl_func_new(1, 1, 0);
	unsigned int tmp;
	struct fl_expr_t **expr = fl_func_rand(func, &tmp);
	fl_func_dump(func);
	fl_expr_set(expr, fl_expr_flt(2));
	fl_func_dump(func);
	fl_func_delete(func);
	*/

	/*
	struct fl_gen_t *gen;
	struct m_rand_t rand = m_rand_init(0);

	gen = fl_gen_new();
	fl_gen_add(gen, fl_inst_new(fl_func_new(2, 1, 0)));

	//uint64_t tm = sys_utime();

	struct fl_weight_t weight = {
		.add = 8.0f,
		.sub = 2.0f,
		.mul = 8.0f,
		.div = 1.0f
	};
	fl_weight_norm(&weight);

	for(int i = 0; i < 5000; i++)
		fl_gen_trial(gen, &weight, &rand);

	//printf("%.1f\n", (sys_utime() - tm) / 1e3);

	printf("len: %d\n", gen->len);
	for(int i = 0; i < gen->len; i++)
		//printf("hash: %016lx\n", gen->arr[i]->hash),
		fl_func_dump(gen->arr[i]->func),printf("\n");

	fl_gen_delete(gen);
	*/

	if(hax_memcnt != 0)
		fprintf(stderr, "allocated memory: %d\n", hax_memcnt), exit(1);

	return 0;
}
