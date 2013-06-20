// Autogenerated AST node
package org.python.antlr.ast;

import org.antlr.runtime.CommonToken;
import org.antlr.runtime.Token;
import org.python.antlr.AST;
import org.python.antlr.PythonTree;
import org.python.antlr.adapter.AstAdapters;
import org.python.antlr.base.excepthandler;
import org.python.antlr.base.expr;
import org.python.antlr.base.mod;
import org.python.antlr.base.slice;
import org.python.antlr.base.stmt;
import org.python.core.ArgParser;
import org.python.core.AstList;
import org.python.core.Py;
import org.python.core.PyObject;
import org.python.core.PyString;
import org.python.core.PyType;
import org.python.expose.ExposedGet;
import org.python.expose.ExposedMethod;
import org.python.expose.ExposedNew;
import org.python.expose.ExposedSet;
import org.python.expose.ExposedType;

import com.oracle.truffle.api.frame.VirtualFrame;

import java.io.DataOutputStream;
import java.io.IOException;
import java.util.ArrayList;

@ExposedType(name = "_ast.BoolOp", base = AST.class)
public class BoolOp extends expr {
    public static final PyType TYPE = PyType.fromClass(BoolOp.class);
    private boolopType op;

    public boolopType getInternalOp() {
        return op;
    }

    @ExposedGet(name = "op")
    public PyObject getOp() {
        return AstAdapters.boolop2py(op);
    }

    @ExposedSet(name = "op")
    public void setOp(PyObject op) {
        this.op = AstAdapters.py2boolop(op);
    }

    private java.util.List<expr> values;

    public java.util.List<expr> getInternalValues() {
        return values;
    }

    @ExposedGet(name = "values")
    public PyObject getValues() {
        return new AstList(values, AstAdapters.exprAdapter);
    }

    @ExposedSet(name = "values")
    public void setValues(PyObject values) {
        this.values = AstAdapters.py2exprList(values);
    }

    private final static PyString[] fields = new PyString[] {
            new PyString("op"), new PyString("values") };

    @ExposedGet(name = "_fields")
    public PyString[] get_fields() {
        return fields;
    }

    private final static PyString[] attributes = new PyString[] {
            new PyString("lineno"), new PyString("col_offset") };

    @ExposedGet(name = "_attributes")
    public PyString[] get_attributes() {
        return attributes;
    }

    public BoolOp(PyType subType) {
        super(subType);
    }

    public BoolOp() {
        this(TYPE);
    }

    @ExposedNew
    @ExposedMethod
    public void BoolOp___init__(PyObject[] args, String[] keywords) {
        ArgParser ap = new ArgParser("BoolOp", args, keywords, new String[] {
                "op", "values", "lineno", "col_offset" }, 2, true);
        setOp(ap.getPyObject(0, Py.None));
        setValues(ap.getPyObject(1, Py.None));
        int lin = ap.getInt(2, -1);
        if (lin != -1) {
            setLineno(lin);
        }

        int col = ap.getInt(3, -1);
        if (col != -1) {
            setLineno(col);
        }

    }

    public BoolOp(PyObject op, PyObject values) {
        setOp(op);
        setValues(values);
    }

    public BoolOp(Token token, boolopType op, java.util.List<expr> values) {
        super(token);
        this.op = op;
        this.values = values;
        if (values == null) {
            this.values = new ArrayList<expr>();
        }
        for (PythonTree t : this.values) {
            addChild(t);
        }
    }

    public BoolOp(Integer ttype, Token token, boolopType op,
            java.util.List<expr> values) {
        super(ttype, token);
        this.op = op;
        this.values = values;
        if (values == null) {
            this.values = new ArrayList<expr>();
        }
        for (PythonTree t : this.values) {
            addChild(t);
        }
    }

    public BoolOp(PythonTree tree, boolopType op, java.util.List<expr> values) {
        super(tree);
        this.op = op;
        this.values = values;
        if (values == null) {
            this.values = new ArrayList<expr>();
        }
        for (PythonTree t : this.values) {
            addChild(t);
        }
    }

    @ExposedGet(name = "repr")
    public String toString() {
        return "BoolOp";
    }

    public String toStringTree() {
        StringBuffer sb = new StringBuffer("BoolOp(");
        sb.append("op=");
        sb.append(dumpThis(op));
        sb.append(",");
        sb.append("values=");
        sb.append(dumpThis(values));
        sb.append(",");
        sb.append(")");
        return sb.toString();
    }

    public <R> R accept(VisitorIF<R> visitor) throws Exception {
        return visitor.visitBoolOp(this);
    }

    public void traverse(VisitorIF<?> visitor) throws Exception {
        if (values != null) {
            for (PythonTree t : values) {
                if (t != null)
                    t.accept(visitor);
            }
        }
    }

    private int lineno = -1;

    @ExposedGet(name = "lineno")
    public int getLineno() {
        if (lineno != -1) {
            return lineno;
        }
        return getLine();
    }

    @ExposedSet(name = "lineno")
    public void setLineno(int num) {
        lineno = num;
    }

    private int col_offset = -1;

    @ExposedGet(name = "col_offset")
    public int getCol_offset() {
        if (col_offset != -1) {
            return col_offset;
        }
        return getCharPositionInLine();
    }

    @ExposedSet(name = "col_offset")
    public void setCol_offset(int num) {
        col_offset = num;
    }

    @Override
    public Object executeGeneric(VirtualFrame frame) {
        // TODO Auto-generated method stub
        return null;
    }

}
