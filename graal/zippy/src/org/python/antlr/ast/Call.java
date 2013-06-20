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
import org.python.ast.datatypes.PArguments;
import org.python.ast.datatypes.PyObjectContainer;
import org.python.core.ArgParser;
import org.python.core.AstList;
import org.python.core.Options;
import org.python.core.Py;
import org.python.core.PyList;
import org.python.core.PyObject;
import org.python.core.PyString;
import org.python.core.PyTuple;
import org.python.core.PyType;
import org.python.core.truffle.ASTInterpreter;
import org.python.core.truffle.GeneratorCallTarget;
import org.python.expose.ExposedGet;
import org.python.expose.ExposedMethod;
import org.python.expose.ExposedNew;
import org.python.expose.ExposedSet;
import org.python.expose.ExposedType;

import com.oracle.truffle.api.Arguments;
import com.oracle.truffle.api.CallTarget;
import com.oracle.truffle.api.frame.VirtualFrame;

import java.io.DataOutputStream;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

@ExposedType(name = "_ast.Call", base = AST.class)
public class Call extends expr {
    public static final PyType TYPE = PyType.fromClass(Call.class);
    private expr func;

    public expr getInternalFunc() {
        return func;
    }

    @ExposedGet(name = "func")
    public PyObject getFunc() {
        return func;
    }

    @ExposedSet(name = "func")
    public void setFunc(PyObject func) {
        // Truffle
        if (Options.specialize) {
            this.func = adoptChild(AstAdapters.py2expr(func));
        } else {
            this.func = AstAdapters.py2expr(func);
        }
    }

    private java.util.List<expr> args;

    public java.util.List<expr> getInternalArgs() {
        return args;
    }

    @ExposedGet(name = "args")
    public PyObject getArgs() {
        return new AstList(args, AstAdapters.exprAdapter);
    }

    @ExposedSet(name = "args")
    public void setArgs(PyObject args) {
        this.args = AstAdapters.py2exprList(args);
    }

    private java.util.List<keyword> keywords;

    public java.util.List<keyword> getInternalKeywords() {
        return keywords;
    }

    @ExposedGet(name = "keywords")
    public PyObject getKeywords() {
        return new AstList(keywords, AstAdapters.keywordAdapter);
    }

    @ExposedSet(name = "keywords")
    public void setKeywords(PyObject keywords) {
        this.keywords = AstAdapters.py2keywordList(keywords);
    }

    private expr starargs;

    public expr getInternalStarargs() {
        return starargs;
    }

    @ExposedGet(name = "starargs")
    public PyObject getStarargs() {
        return starargs;
    }

    @ExposedSet(name = "starargs")
    public void setStarargs(PyObject starargs) {
        this.starargs = AstAdapters.py2expr(starargs);
    }

    private expr kwargs;

    public expr getInternalKwargs() {
        return kwargs;
    }

    @ExposedGet(name = "kwargs")
    public PyObject getKwargs() {
        return kwargs;
    }

    @ExposedSet(name = "kwargs")
    public void setKwargs(PyObject kwargs) {
        this.kwargs = AstAdapters.py2expr(kwargs);
    }

    private final static PyString[] fields = new PyString[] {
            new PyString("func"), new PyString("args"),
            new PyString("keywords"), new PyString("starargs"),
            new PyString("kwargs") };

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

    public Call(PyType subType) {
        super(subType);
    }

    public Call() {
        this(TYPE);
    }

    @ExposedNew
    @ExposedMethod
    public void Call___init__(PyObject[] args, String[] keywords) {
        ArgParser ap = new ArgParser("Call", args, keywords, new String[] {
                "func", "args", "keywords", "starargs", "kwargs", "lineno",
                "col_offset" }, 5, true);
        setFunc(ap.getPyObject(0, Py.None));
        setArgs(ap.getPyObject(1, Py.None));
        setKeywords(ap.getPyObject(2, Py.None));
        setStarargs(ap.getPyObject(3, Py.None));
        setKwargs(ap.getPyObject(4, Py.None));
        int lin = ap.getInt(5, -1);
        if (lin != -1) {
            setLineno(lin);
        }

        int col = ap.getInt(6, -1);
        if (col != -1) {
            setLineno(col);
        }

    }

    public Call(PyObject func, PyObject args, PyObject keywords,
            PyObject starargs, PyObject kwargs) {
        setFunc(func);
        setArgs(args);
        setKeywords(keywords);
        setStarargs(starargs);
        setKwargs(kwargs);
    }

    public Call(Token token, expr func, java.util.List<expr> args,
            java.util.List<keyword> keywords, expr starargs, expr kwargs) {
        super(token);
        this.func = func;
        addChild(func);
        this.args = args;
        if (args == null) {
            this.args = new ArrayList<expr>();
        }
        for (PythonTree t : this.args) {
            addChild(t);
        }
        this.keywords = keywords;
        if (keywords == null) {
            this.keywords = new ArrayList<keyword>();
        }
        for (PythonTree t : this.keywords) {
            addChild(t);
        }
        this.starargs = starargs;
        addChild(starargs);
        this.kwargs = kwargs;
        addChild(kwargs);
    }

    public Call(Integer ttype, Token token, expr func,
            java.util.List<expr> args, java.util.List<keyword> keywords,
            expr starargs, expr kwargs) {
        super(ttype, token);
        this.func = func;
        addChild(func);
        this.args = args;
        if (args == null) {
            this.args = new ArrayList<expr>();
        }
        for (PythonTree t : this.args) {
            addChild(t);
        }
        this.keywords = keywords;
        if (keywords == null) {
            this.keywords = new ArrayList<keyword>();
        }
        for (PythonTree t : this.keywords) {
            addChild(t);
        }
        this.starargs = starargs;
        addChild(starargs);
        this.kwargs = kwargs;
        addChild(kwargs);
    }

    public Call(PythonTree tree, expr func, java.util.List<expr> args,
            java.util.List<keyword> keywords, expr starargs, expr kwargs) {
        super(tree);
        this.func = func;
        addChild(func);
        this.args = args;
        if (args == null) {
            this.args = new ArrayList<expr>();
        }
        for (PythonTree t : this.args) {
            addChild(t);
        }
        this.keywords = keywords;
        if (keywords == null) {
            this.keywords = new ArrayList<keyword>();
        }
        for (PythonTree t : this.keywords) {
            addChild(t);
        }
        this.starargs = starargs;
        addChild(starargs);
        this.kwargs = kwargs;
        addChild(kwargs);
    }

    @ExposedGet(name = "repr")
    public String toString() {
        return "Call";
    }

    public String toStringTree() {
        StringBuffer sb = new StringBuffer("Call(");
        sb.append("func=");
        sb.append(dumpThis(func));
        sb.append(",");
        sb.append("args=");
        sb.append(dumpThis(args));
        sb.append(",");
        sb.append("keywords=");
        sb.append(dumpThis(keywords));
        sb.append(",");
        sb.append("starargs=");
        sb.append(dumpThis(starargs));
        sb.append(",");
        sb.append("kwargs=");
        sb.append(dumpThis(kwargs));
        sb.append(",");
        sb.append(")");
        return sb.toString();
    }

    public <R> R accept(VisitorIF<R> visitor) throws Exception {
        return visitor.visitCall(this);
    }

    public void traverse(VisitorIF<?> visitor) throws Exception {
        if (func != null)
            func.accept(visitor);
        if (args != null) {
            for (int i = 0; i < args.size(); i++) {
                PythonTree t = args.get(i);
                // modified due to ConcurrentModificationException
//              for (PythonTree t : args) {
                if (t != null)
                    t.accept(visitor);
            }
        }
        if (keywords != null) {
            for (PythonTree t : keywords) {
                if (t != null)
                    t.accept(visitor);
            }
        }
        if (starargs != null)
            starargs.accept(visitor);
        if (kwargs != null)
            kwargs.accept(visitor);
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
        Object callable = func.executeGeneric(frame);
        List<PyObject> arguments = doArguments(frame);
        Object returnValue = null;

        if (callable instanceof PyObject) {
            /*
             * traditional Jython style invocation
             */
            if (ASTInterpreter.debug) {
                System.out.println("CALL J " + callable);
            }

            returnValue = ((PyObject) callable).__call__(arguments.toArray(new PyObject[arguments.size()]));
        } else if (callable instanceof CallTarget) {
            /*
             * Truffle style invocation
             */
            if (ASTInterpreter.debug) {
                System.out.println("CALL T " + callable);
            }

            // fix tuple issue --chen
            List<PyObject> tupleElements = new ArrayList<PyObject>();
            for (PyObject arg : arguments) {
                if (arg instanceof PyTuple) {
                    for (PyObject o : ((PyTuple) arg).getArray()) {
                        tupleElements.add(o);
                    }
                }
            }

            for (PyObject o : tupleElements) {
                arguments.add(o);
            }

            Arguments truffleArguments = new PArguments(arguments.toArray(new PyObject[arguments.size()]));
            returnValue = ((CallTarget) callable).call(null, truffleArguments);
        } else {
            throw Py.SystemError("Unexpected callable type");
        }

        return returnValue;
    }

    private List<PyObject> doArguments(VirtualFrame frame) {
        List<PyObject> arguments = new ArrayList<PyObject>();

        for (expr e : args) {
            Object arg = e.executeGeneric(frame);
            if (arg instanceof PyObject) {
                arguments.add((PyObject) arg);
            } else {
                arguments.add(PyObjectContainer.pack(arg));
            }
        }

        return arguments;
    }
}
