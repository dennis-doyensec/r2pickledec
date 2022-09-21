#include "dump.h"

static inline void printer_drain(PrintInfo *nfo) {
	r_return_if_fail (nfo->out);
	if (r_strbuf_length (nfo->out)) {
		char *buf = r_strbuf_drain_nofree (nfo->out);
		if (buf) {
			r_cons_printf ("%s", buf);
			free (buf);
		}
	}
}

static inline void printer_drain_free(PrintInfo *nfo) {
	printer_drain (nfo);
	r_strbuf_free (nfo->out);
	nfo->out = NULL;
}

static inline RStrBuf *printer_getout(PrintInfo *nfo) {
	if (!nfo->out) {
		nfo->out = r_strbuf_new ("");
	}
	return nfo->out;
}

static inline bool printer_append(PrintInfo *nfo, const char *str) {
	RStrBuf *buf = printer_getout (nfo);
	if (buf && r_strbuf_append (buf, str)) {
		return true;
	}
	R_LOG_ERROR ("Failed to append to buffer");
	return false;
}

static inline bool printer_appendf(PrintInfo *nfo, const char *fmt, ...) {
	r_return_val_if_fail (nfo && fmt, false);
	RStrBuf *buf = printer_getout (nfo);
	if (buf) {
		va_list ap;
		va_start (ap, fmt);
		bool ret = r_strbuf_vappendf (buf, fmt, ap);
		va_end (ap);
		return ret;
	}

	R_LOG_ERROR ("Failed to append to buffer");
	return false;
}

static inline const char *obj_varname(PrintInfo *nfo, PyObj *obj) {
	if (!obj->varname) {
		st64 num = obj->memo_id? obj->memo_id: nfo->varid++;
		obj->varname = r_str_newf ("var_%"PFMT64x, num);
	}
	return obj->varname;
}

// prepend nfo->out with obj declaration, then write obj name
static inline bool prepend_obj(PrintInfo *nfo, PyObj *obj) {
	r_return_val_if_fail (!nfo->first, false);
	// save old state
	bool nforet = nfo->ret;
	if (!r_list_push (nfo->outstack, nfo->out)) {
		return false;
	}

	// prepends always start the line, never return
	nfo->first = true;
	nfo->ret = false;
	nfo->out = NULL;

	bool ret = dump_obj (nfo, obj);

	// restore prev state with previous buffer
	printer_drain_free (nfo);
	nfo->out = r_list_pop (nfo->outstack);
	nfo->first = false;
	nfo->ret = nforet;

	if (ret) {
		return printer_append (nfo, obj->varname);
	}
	return false;
}

#define PREPRINT() {\
	int o = var_pre_print (nfo, obj); \
	if (o) { \
		if (o < 0) { \
			R_LOG_ERROR ("Alloc failed"); \
			return false; \
		} \
		return true; \
	} \
}

// 0 ok, >0 printed var instead of obj (ie caller is done), <0 error
static inline int var_pre_print(PrintInfo *nfo, PyObj *obj) {
	if (nfo->ret) {
		printer_append (nfo, "return ");
		if (obj->varname) {
			if (!printer_appendf (nfo, "%s\n", obj->varname)) {
				return -1;
			}
			return 1;
		}
		return 0;
	}

	if (nfo->first) {
		if (obj->varname) {
			if (nfo->verbose) { // debug
				if (!printer_appendf (nfo, "# %s previously declared\n", obj->varname)) {
					return -1;
				}
			}
			return 1;
		}
		const char *var = obj_varname (nfo, obj);
		if (!var) {
			return -1;
		}
		if (!printer_appendf (nfo, "%s = ", var)) {
			return -1;
		}
		return 0;
	}

	if (obj->varname) {
		if (!printer_append (nfo, obj->varname)) {
			return -1;
		}
		return 1;
	}
	return 0;
}

static inline bool newline(PrintInfo *nfo, PyObj *obj) {
	if (nfo->first || nfo->ret) {
		if (!printer_append (nfo, "\n")) {
			return false;
		}
	}
	return true;
}

static inline bool dump_bool(PrintInfo *nfo, PyObj *obj) {
	PREPRINT ();
	bool ret = printer_append (nfo, obj->py_bool? "True": "False");
	ret &= newline (nfo, obj);
	return ret;
}

static inline bool dump_int(PrintInfo *nfo, PyObj *obj) {
	PREPRINT ();
	bool ret = printer_appendf (nfo, "%d", obj->py_int);
	ret &= newline (nfo, obj);
	return ret;
}

static inline bool dump_str(PrintInfo *nfo, PyObj *obj) {
	PREPRINT ();
	bool ret = printer_append (nfo, obj->py_str);
	ret &= newline (nfo, obj);
	return ret;
}

static inline bool dump_float(PrintInfo *nfo, PyObj *obj) {
	PREPRINT ();
	bool ret = printer_appendf (nfo, "%lf", obj->py_float);
	ret &= newline (nfo, obj);
	return ret;
}

static inline bool dump_none(PrintInfo *nfo, PyObj *obj) {
	PREPRINT ();
	bool ret = printer_append (nfo, "None");
	ret &= newline (nfo, obj);
	return ret;
}

static inline bool dump_iter(PrintInfo *nfo, PyObj *obj_iter) {
	// recursees, so save and modify nfo state
	bool nfofirst = nfo->first;
	bool nforet = nfo->ret;
	nfo->first = false;
	nfo->ret = false;

	bool ret = true;
	PyObj *obj;
	RListIter *iter;
	r_list_foreach (obj_iter->py_iter, iter, obj) {
		ret &= dump_obj (nfo, obj);
		if (!ret) {
			break;
		}
		if (iter != r_list_tail (obj_iter->py_iter)) {
			ret &= printer_append (nfo, ", ");
		}
	}
	nfo->first = nfofirst;
	nfo->ret = nforet;
	return ret;
}

static inline bool dump_tuple(PrintInfo *nfo, PyObj *obj) {
	PREPRINT ();
	bool ret = printer_append (nfo, "(");
	ret &= dump_iter (nfo, obj);
	ret &= printer_append (nfo, ")");
	ret &= newline(nfo, obj);
	return ret;
}

static inline bool dump_list(PrintInfo *nfo, PyObj *obj) {
	PREPRINT ();
	bool ret = printer_append (nfo, "[");
	ret &= dump_iter (nfo, obj);
	ret &= printer_append (nfo, "]");
	ret &= newline(nfo, obj);
	return ret;
}

static inline bool dump_iter_dict(PrintInfo *nfo, PyObj *obj_iter) {
	// recursees, so save and modify nfo state
	bool nfofirst = nfo->first;
	bool nforet = nfo->ret;
	nfo->first = false;
	nfo->ret = false;

	bool onkey = true;
	bool ret = true;
	PyObj *obj;
	RListIter *iter;
	r_list_foreach (obj_iter->py_iter, iter, obj) {
		ret &= dump_obj (nfo, obj);
		if (!ret) {
			break;
		}
		if (onkey) {
			ret &= printer_append (nfo, ": ");
		} else if (iter != r_list_tail (obj_iter->py_iter)) {
			ret &= printer_append (nfo, ", ");
		}
		onkey = !onkey;
	}
	nfo->first = nfofirst;
	nfo->ret = nforet;
	return ret;
}

static inline bool dump_dict(PrintInfo *nfo, PyObj *obj) {
	PREPRINT ();
	bool ret = printer_append (nfo, "{");
	ret &= dump_iter_dict (nfo, obj);
	ret &= printer_append (nfo, "}");
	ret &= newline(nfo, obj);
	return ret;
}

static inline bool dump_func(PrintInfo *nfo, PyObj *obj) {
	PREPRINT ();
	bool ret = printer_appendf (nfo, "__import__(\"%s\").%s", obj->py_func.module, obj->py_func.name);
	ret &= newline(nfo, obj);
	return ret;
}

static inline bool dump_oper_init(PrintInfo *nfo, PyOper *pop, RStrBuf *pre, const char *vn) {
	if (
		!printer_appendf (nfo, "%s = ", vn)
		|| !dump_obj (nfo, r_list_last (pop->stack))
		|| !printer_append (nfo, "\n")
	) {
		return false;
	}
	return true;
}

static inline bool dump_oper_reduce(PrintInfo *nfo, PyOper *pop, RStrBuf *pre, const char *vn) {
	PyObj *args = r_list_last (pop->stack);
	if (args && args->type != PY_TUPLE) {
		R_LOG_ERROR ("Reduce requires tuple")
	} else if (
		!printer_appendf (nfo, "%s = %s", vn, vn)
		|| !dump_obj (nfo, args)
		|| !printer_append (nfo, "\n")
	) {
		return false;
	}
	return true;
}

static inline bool dump_oper_newobj(PrintInfo *nfo, PyOper *pop, RStrBuf *pre, const char *vn) {
	PyObj *args = r_list_last (pop->stack);
	if (args->type != PY_TUPLE) {
		R_LOG_ERROR ("Reduce requires tuple")
	} else if (
		!printer_appendf (nfo, "%s = %s.__new__(%s, *", vn, vn, vn)
		|| !dump_obj (nfo, args)
		|| !printer_append (nfo, ")\n")
	) {
		return false;
	}
	return true;
}

static inline bool dump_oper(PrintInfo *nfo, PyOper *pop, RStrBuf *pre, const char *vn) {
	switch (pop->op) {
	case OP_FAKE_INIT:
		return dump_oper_init (nfo, pop, pre, vn);
	case OP_REDUCE:
		return dump_oper_reduce (nfo, pop, pre, vn);
	case OP_NEWOBJ:
		return dump_oper_newobj (nfo, pop, pre, vn);
	default:
		R_LOG_ERROR ("Can't parse type %s (%02x) yet", py_op_to_name (pop->op), pop->op & 0xff);
	}
	return false;
}

static inline bool dump_what(PrintInfo *nfo, PyObj *obj) {
	if (!nfo->first) {
		if (obj->varname) {
			return printer_append (nfo, obj->varname);
		}
		return prepend_obj (nfo, obj);
	}
	// it is first object
	if (obj->varname) {
		if (nfo->ret && !printer_append (nfo, "return ")) {
			return false;
		}

		if (nfo->ret && printer_appendf (nfo, "return %s\n", obj->varname)) {
			return true;
		} else {

		}
	}
	// populate obj->varname
	if (!obj_varname (nfo, obj)) {
		return false;
	}

	RStrBuf *pre = printer_getout (nfo); // for prepending objects
	if (!pre) {
		return false;
	}
	nfo->out = NULL; // will be auto populated, joined with pre before leaving

	bool nforet = nfo->ret; // save state, we know nfo->first is true
	nfo->ret = false;
	nfo->first = false;

	bool ret = true;
	PyOper *pop;
	RListIter *iter;
	r_list_foreach (obj->py_what, iter, pop) {
		if (!dump_oper (nfo, pop, pre, obj->varname)) {
			ret = false;
			break;
		}
	}
	nfo->first = true;
	nfo->ret = nforet;
	if (ret) {
		if (nfo->ret) {
			ret &= printer_appendf (nfo, "return %s\n", obj->varname);
		}

		// combine pre and out
		if (r_strbuf_length (nfo->out)) {
			char *buf = r_strbuf_drain (nfo->out);
			nfo->out = pre;
			if (!buf) {
				return false;
			}
			ret &= printer_append (nfo, buf);
			free (buf);
		}
	}
	return ret;
}

bool dump_obj(PrintInfo *nfo, PyObj *obj) {
	switch (obj->type) {
	case PY_BOOL:
		return dump_bool (nfo, obj);
	case PY_INT:
		return dump_int (nfo, obj);
	case PY_STR:
		return dump_str (nfo, obj);
	case PY_FLOAT:
		return dump_float (nfo, obj);
	case PY_NONE:
		return dump_none (nfo, obj);
	case PY_TUPLE:
		return dump_tuple (nfo, obj);
	case PY_LIST:
		return dump_list (nfo, obj);
	case PY_DICT:
		return dump_dict (nfo, obj);
	case PY_FUNC:
		return dump_func (nfo, obj);
	case PY_WHAT:
		return dump_what (nfo, obj);
	default:
		R_LOG_ERROR ("Can't handle type %s", py_type_to_name(obj->type))
	}
	return false;
}

static inline bool dump_stack(PrintInfo *nfo, RList *stack, const char *n) {
	int len = r_list_length (stack);
	if (len == 0) {
		printer_appendf (nfo, "## stack %s empty\n", n);
	}
	RListIter *iter;
	PyObj *obj;
	printer_appendf (nfo, "## Stack %s start, len %d\n", n, len);
	r_list_foreach (stack, iter, obj) {
		len--;
		printer_appendf (nfo, "## %s[%d] %s\n", n, len, len == 0? "TOP": "");
		printer_drain (nfo);

		nfo->first = true;
		if (len == 0) {
			nfo->ret = true;
		}
		if (!dump_obj (nfo, obj)) {
			return false;
		}
		printer_drain (nfo);
	}
	return true;
}

bool dump_machine(PMState *pvm, PrintInfo *nfo) {
	nfo->verbose = true; // temp for debuging
	nfo->outstack = r_list_newf ((RListFree)r_strbuf_free);
	if (!nfo->outstack) {
		return false;
	}
	bool ret = true;
	if (nfo->stack) {
		ret &= dump_stack (nfo, pvm->stack, "VM");
	}
	printer_drain_free (nfo);
	return ret;
}

const char *py_type_to_name(PyType t) {
	switch (t) {
	case PY_WHAT:
		return "PY_WHAT";
	case PY_NONE:
		return "PY_NONE";
	case PY_INT:
		return "PY_INT";
	case PY_FLOAT:
		return "PY_FLOAT";
	case PY_STR:
		return "PY_STR";
	case PY_FUNC:
		return "PY_FUNC";
	case PY_TUPLE:
		return "PY_TUPLE";
	case PY_LIST:
		return "PY_LIST";
	case PY_BOOL:
		return "PY_BOOL";
	case PY_DICT:
		return "PY_DICT";
	case PY_NOT_RIGHT:
	default:
		r_warn_if_reached ();
		return "UNKOWN";
	}
}

const char *py_op_to_name(PyOp t) {
	switch (t) {
	case OP_REDUCE:
		return "reduce";
	case OP_BUILD:
		return "build";
	case OP_NEWOBJ:
		return "newobj";
	case OP_NEWOBJ_EX:
		return "newobj_ex";
	case OP_APPEND:
		return "append";
	case OP_SETITEM:
		return "setitem";
	case OP_FAKE_INIT:
		return "Initial Object";
	case OP_SETITEMS:
		return "setitems";
	case OP_APPENDS:
		return "appends";
	default:
		R_LOG_ERROR ("Unkown opcode %d", t);
		r_warn_if_reached ();
		return "UNKOWN OPCODE";
	}
}