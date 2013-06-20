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
import org.python.ast.utils.*;
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

@ExposedType(name = "_ast.Break", base = AST.class)
public class Break extends stmt {
    public static final PyType TYPE = PyType.fromClass(Break.class);

    private final static PyString[] fields = new PyString[0];
    
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

    public Break(PyType subType) {
        super(subType);
    }

    @ExposedNew
    @ExposedMethod
    public void Break___init__(PyObject[] args, String[] keywords) {
        ArgParser ap = new ArgParser("Break", args, keywords, new String[] {
                "lineno", "col_offset" }, 0, true);
        int lin = ap.getInt(0, -1);
        if (lin != -1) {
            setLineno(lin);
        }

        int col = ap.getInt(1, -1);
        if (col != -1) {
            setLineno(col);
        }

    }

    public Break() {
    }

    public Break(Token token) {
        super(token);
    }

    public Break(Integer ttype, Token token) {
        super(ttype, token);
    }

    public Break(PythonTree tree) {
        super(tree);
    }

    @ExposedGet(name = "repr")
    public String toString() {
        return "Break";
    }

    public String toStringTree() {
        StringBuffer sb = new StringBuffer("Break(");
        sb.append(")");
        return sb.toString();
    }

    public <R> R accept(VisitorIF<R> visitor) throws Exception {
        return visitor.visitBreak(this);
    }

    public void traverse(VisitorIF<?> visitor) throws Exception {
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
    public void executeVoid(VirtualFrame frame) {
    	throw BreakException.breakException;    
    }

}
