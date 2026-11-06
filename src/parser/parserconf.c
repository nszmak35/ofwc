#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "parserconf.h"

typedef struct {
	OfwcEntryCb cb;
	void *userdata;
	int source;
} Parser;

static PyObject *
stringify(PyObject *value)
{
	if (value == Py_None)
		return PyUnicode_FromString("NULL");
	if (PyBool_Check(value))
		return PyUnicode_FromString(value == Py_True ? "1" : "0");
	return PyObject_Str(value);
}

static int
emit_row(Parser *p, OfwcType type, const char *key, PyObject *seq)
{
	Py_ssize_t n, i, made = 0;
	const char **items;
	PyObject **strings;
	OfwcEntry e;
	int rc = -1;

	if (!PyList_Check(seq) && !PyTuple_Check(seq))
		return 0;
	n = PySequence_Size(seq);
	if (n < 0) {
		PyErr_Clear();
		return -1;
	}
	if (n == 0)
		return 0;

	items = calloc((size_t)n, sizeof(*items));
	strings = calloc((size_t)n, sizeof(*strings));
	if (!items || !strings)
		goto out;

	for (i = 0; i < n; i++) {
		PyObject *v = PySequence_GetItem(seq, i);
		if (!v)
			goto out;
		strings[i] = stringify(v);
		Py_DECREF(v);
		if (!strings[i])
			goto out;
		made++;
		items[i] = PyUnicode_AsUTF8(strings[i]);
		if (!items[i])
			goto out;
	}

	memset(&e, 0, sizeof(e));
	e.type = type;
	e.key = key;
	e.items = items;
	e.count = (size_t)n;
	e.source = p->source;
	rc = p->cb(&e, p->userdata);

out:
	for (i = 0; i < made; i++)
		Py_XDECREF(strings[i]);
	free(strings);
	free(items);
	return rc;
}

static int
emit_scalar(Parser *p, const char *key, PyObject *value)
{
	PyObject *s = stringify(value);
	const char *text;
	OfwcEntry e;
	int rc;

	if (!s)
		return -1;
	text = PyUnicode_AsUTF8(s);
	if (!text) {
		Py_DECREF(s);
		return -1;
	}
	memset(&e, 0, sizeof(e));
	e.type = OFWC_SCALAR;
	e.key = key;
	e.value = text;
	e.source = p->source;
	rc = p->cb(&e, p->userdata);
	Py_DECREF(s);
	return rc;
}

static int
emit_rules(Parser *p, PyObject *value)
{
	Py_ssize_t n, i;

	if (!PyList_Check(value) && !PyTuple_Check(value))
		return 0;
	n = PySequence_Size(value);
	if (n < 0) {
		PyErr_Clear();
		return -1;
	}
	for (i = 0; i < n; i++) {
		static const char *names[] = {"appid", "title", "workspace",
		                              "floating", "fullscreen", "monitor"};
		PyObject *item = PySequence_GetItem(value, i);
		PyObject *row;
		int rc, j;

		if (!item)
			return -1;
		if (!PyDict_Check(item)) {
			Py_DECREF(item);
			continue;
		}
		row = PyTuple_New(6);
		if (!row) {
			Py_DECREF(item);
			return -1;
		}
		for (j = 0; j < 6; j++) {
			PyObject *v = PyDict_GetItemString(item, names[j]);
			if (v)
				Py_INCREF(v);
			else if (j == 2 || j == 5)
				v = PyLong_FromLong(j == 2 ? 0 : -1);
			else if (j == 3 || j == 4) {
				v = Py_False;
				Py_INCREF(v);
			} else {
				v = Py_None;
				Py_INCREF(v);
			}
			if (!v) {
				Py_DECREF(row);
				Py_DECREF(item);
				return -1;
			}
			PyTuple_SET_ITEM(row, j, v);
		}
		rc = emit_row(p, OFWC_TABLE, "rules", row);
		Py_DECREF(row);
		Py_DECREF(item);
		if (rc)
			return rc;
	}
	return 0;
}

static int
emit_list(Parser *p, const char *key, PyObject *value)
{
	Py_ssize_t n, i;

	if (!PyList_Check(value) && !PyTuple_Check(value))
		return 0;
	n = PySequence_Size(value);
	if (n < 0) {
		PyErr_Clear();
		return -1;
	}
	for (i = 0; i < n; i++) {
		PyObject *item = PySequence_GetItem(value, i);
		PyObject *row;
		int rc;

		if (!item)
			return -1;
		if (PyList_Check(item) || PyTuple_Check(item)) {
			rc = emit_row(p, OFWC_TABLE, key, item);
		} else {
			row = PyTuple_Pack(1, item);
			if (!row) {
				Py_DECREF(item);
				return -1;
			}
			rc = emit_row(p, OFWC_LIST, key, row);
			Py_DECREF(row);
		}
		Py_DECREF(item);
		if (rc)
			return rc;
	}
	return 0;
}

static int
emit_class(Parser *p, PyObject *klass)
{
	PyObject *mapping = PyObject_GetAttrString(klass, "__dict__");
	PyObject *keys;
	Py_ssize_t n, i;
	int rc = 0;

	if (!mapping) {
		PyErr_Clear();
		return 0;
	}
	keys = PyMapping_Keys(mapping);
	if (!keys) {
		Py_DECREF(mapping);
		return -1;
	}
	n = PySequence_Size(keys);
	for (i = 0; i < n && !rc; i++) {
		PyObject *key = PySequence_GetItem(keys, i);
		PyObject *value;
		const char *name;

		if (!key) {
			rc = -1;
			break;
		}
		name = PyUnicode_Check(key) ? PyUnicode_AsUTF8(key) : NULL;
		if (!name || name[0] == '_') {
			PyErr_Clear();
			Py_DECREF(key);
			continue;
		}
		value = PyObject_GetItem(mapping, key);
		Py_DECREF(key);
		if (!value) {
			PyErr_Clear();
			continue;
		}
		if (PyList_Check(value) || PyTuple_Check(value) ||
		    PyDict_Check(value) || PyCallable_Check(value))
			rc = 0;
		else
			rc = emit_scalar(p, name, value);
		Py_DECREF(value);
	}
	Py_DECREF(keys);
	Py_DECREF(mapping);
	return rc;
}

static int
emit_autostart(Parser *p, PyObject *value)
{
	Py_ssize_t n, i;

	if (!PyList_Check(value) && !PyTuple_Check(value))
		return 0;
	n = PySequence_Size(value);
	for (i = 0; i < n; i++) {
		PyObject *item = PySequence_GetItem(value, i);
		PyObject *row;
		int rc;

		if (!item)
			return -1;
		if (!PyUnicode_Check(item)) {
			Py_DECREF(item);
			continue;
		}
		row = PyTuple_Pack(1, item);
		Py_DECREF(item);
		if (!row)
			return -1;
		rc = emit_row(p, OFWC_TABLE, "autostart", row);
		Py_DECREF(row);
		if (rc)
			return rc;
	}
	return 0;
}

static int
emit_tags(Parser *p, PyObject *value)
{
	if (PyLong_Check(value) && !PyBool_Check(value)) {
		long count = PyLong_AsLong(value), i;

		if (PyErr_Occurred()) {
			PyErr_Clear();
			return -1;
		}
		if (count < 1)
			count = 1;
		if (count > 31)
			count = 31;
		for (i = 1; i <= count; i++) {
			PyObject *s = PyUnicode_FromFormat("%ld", i);
			PyObject *row;
			int rc;

			if (!s)
				return -1;
			row = PyTuple_Pack(1, s);
			Py_DECREF(s);
			if (!row)
				return -1;
			rc = emit_row(p, OFWC_LIST, "tags", row);
			Py_DECREF(row);
			if (rc)
				return rc;
		}
		return 0;
	}
	return emit_list(p, "tags", value);
}

static int
parse_globals(Parser *p, PyObject *globals)
{
	PyObject *key, *value;
	Py_ssize_t pos = 0;

	while (PyDict_Next(globals, &pos, &key, &value)) {
		const char *name;
		int rc;

		if (!PyUnicode_Check(key))
			continue;
		name = PyUnicode_AsUTF8(key);
		if (!name || name[0] == '_') {
			PyErr_Clear();
			continue;
		}
		if (PyModule_Check(value) || PyCallable_Check(value)) {
			if (PyType_Check(value)) {
				if (!strcmp(name, "Bar"))
					continue;
				rc = emit_class(p, value);
				if (rc)
					return rc;
			}
			continue;
		}
		if (!strcmp(name, "tags"))
			rc = emit_tags(p, value);
		else if (!strcmp(name, "autostart"))
			rc = emit_autostart(p, value);
		else if (!strcmp(name, "rules"))
			rc = emit_rules(p, value);
		else if (!strcmp(name, "monrules") || !strcmp(name, "keys") ||
		         !strcmp(name, "buttons"))
			rc = emit_list(p, name, value);
		else if (PyList_Check(value) || PyTuple_Check(value) ||
		         PyDict_Check(value) || PyType_Check(value))
			rc = 0;
		else
			rc = emit_scalar(p, name, value);
		if (rc)
			return rc;
	}
	return 0;
}

static void
py_init_once(void)
{
	if (!Py_IsInitialized()) {
		Py_InitializeEx(0);
	}
}

/* A config may import modules next to it; stale cached ones are dropped on reload. */
static PyObject *
cfgdir_enter(const char *name)
{
	const char *slash;
	PyObject *dir, *path, *mods, *items, *importlib;
	Py_ssize_t i, n, dlen;

	if (!name || name[0] != '/' || !(slash = strrchr(name, '/')))
		return NULL;
	dlen = slash == name ? 1 : (Py_ssize_t)(slash - name);
	dir = PyUnicode_FromStringAndSize(name, dlen);
	if (!dir) {
		PyErr_Clear();
		return NULL;
	}
	path = PySys_GetObject("path");
	if (!path || !PyList_Check(path) || PyList_Insert(path, 0, dir) < 0) {
		PyErr_Clear();
		Py_DECREF(dir);
		return NULL;
	}

	mods = PyImport_GetModuleDict();
	items = mods ? PyDict_Items(mods) : NULL;
	if (items) {
		n = PyList_Size(items);
		for (i = 0; i < n; i++) {
			PyObject *item = PyList_GetItem(items, i);
			PyObject *key, *mod, *file;
			const char *f;

			if (!item || !PyTuple_Check(item) || PyTuple_Size(item) != 2)
				continue;
			key = PyTuple_GetItem(item, 0);
			mod = PyTuple_GetItem(item, 1);
			file = PyObject_GetAttrString(mod, "__file__");
			if (!file) {
				PyErr_Clear();
				continue;
			}
			f = PyUnicode_Check(file) ? PyUnicode_AsUTF8(file) : NULL;
			if (f && !strncmp(f, name, (size_t)dlen) &&
			    (dlen == 1 || f[dlen] == '/')) {
				if (PyDict_DelItem(mods, key) < 0)
					PyErr_Clear();
			} else if (!f) {
				PyErr_Clear();
			}
			Py_DECREF(file);
		}
		Py_DECREF(items);
	} else {
		PyErr_Clear();
	}

	importlib = PyImport_ImportModule("importlib");
	if (importlib) {
		PyObject *r = PyObject_CallMethod(importlib, "invalidate_caches", NULL);
		if (!r)
			PyErr_Clear();
		Py_XDECREF(r);
		Py_DECREF(importlib);
	} else {
		PyErr_Clear();
	}
	return dir;
}

static void
cfgdir_leave(PyObject *dir)
{
	PyObject *path, *r;

	if (!dir)
		return;
	path = PySys_GetObject("path");
	if (path && PyList_Check(path)) {
		r = PyObject_CallMethod(path, "remove", "O", dir);
		if (!r)
			PyErr_Clear();
		Py_XDECREF(r);
	}
	Py_DECREF(dir);
}

static int parse_buffer_impl(const char *src, const char *name, int source,
                             OfwcEntryCb cb, void *userdata);

int
ofwc_parse_buffer(const char *src, const char *name, int source,
                  OfwcEntryCb cb, void *userdata)
{
	PyObject *dir;
	int rc;

	if (!src || !cb)
		return -1;
	py_init_once();
	dir = source ? cfgdir_enter(name) : NULL;
	rc = parse_buffer_impl(src, name, source, cb, userdata);
	cfgdir_leave(dir);
	return rc;
}

static int
parse_buffer_impl(const char *src, const char *name, int source,
                  OfwcEntryCb cb, void *userdata)
{
	Parser p;
	PyObject *globals, *code, *result;
	int rc;

	if (!src || !cb)
		return -1;
	py_init_once();

	p.cb = cb;
	p.userdata = userdata;
	p.source = source;

	globals = PyDict_New();
	if (!globals)
		return -1;
	if (PyDict_SetItemString(globals, "__builtins__", PyEval_GetBuiltins()) < 0) {
		PyErr_Print();
		Py_DECREF(globals);
		return -1;
	}
	{
		PyObject *nm = PyUnicode_FromString("ofwc_config");
		if (nm) {
			PyDict_SetItemString(globals, "__name__", nm);
			Py_DECREF(nm);
		}
	}

	code = Py_CompileStringExFlags(src, name ? name : "<config>", Py_file_input,
	                               NULL, -1);
	if (!code) {
		PyErr_Print();
		Py_DECREF(globals);
		return -1;
	}
	result = PyEval_EvalCode(code, globals, globals);
	Py_DECREF(code);
	if (!result) {
		PyErr_Print();
		Py_DECREF(globals);
		return -1;
	}
	Py_DECREF(result);

	rc = parse_globals(&p, globals);
	if (rc && PyErr_Occurred())
		PyErr_Print();
	Py_DECREF(globals);
	return rc ? -1 : 0;
}

int
ofwc_parse_file(const char *path, int source, OfwcEntryCb cb, void *userdata,
                int *found)
{
	FILE *fp;
	long size;
	char *buf;
	int rc;

	if (found)
		*found = 0;
	if (!path || access(path, R_OK) != 0)
		return 0;
	fp = fopen(path, "rb");
	if (!fp)
		return 0;
	if (found)
		*found = 1;
	if (fseek(fp, 0, SEEK_END) != 0 || (size = ftell(fp)) < 0 ||
	    size > 4 * 1024 * 1024 || fseek(fp, 0, SEEK_SET) != 0) {
		fclose(fp);
		return -1;
	}
	buf = malloc((size_t)size + 1);
	if (!buf) {
		fclose(fp);
		return -1;
	}
	if (fread(buf, 1, (size_t)size, fp) != (size_t)size) {
		free(buf);
		fclose(fp);
		return -1;
	}
	buf[size] = '\0';
	fclose(fp);
	rc = ofwc_parse_buffer(buf, path, source, cb, userdata);
	free(buf);
	return rc;
}
