package org.python.core;

import static com.sun.max.vm.compiler.CallEntryPoint.OPTIMIZED_ENTRY_POINT;
import com.sun.max.vm.Intrinsics;
import com.sun.max.vm.compiler.target.TargetMethod;

public class PyThreadedCodeInterpreterUSF extends PyBytecode {

    static {
        IOpsCodeTableUSF.instance().generateICT();
    }

    private TargetMethod targetMethod;

    PyThreadedCodeInterpreterUSF() {
        super(false);
        targetMethod = null;
    }

    public PyThreadedCodeInterpreterUSF(PyBytecode pyBytecode) {
        super(pyBytecode, false);
    }

    @Override
    public PyObject interpret(PyFrame frame, ThreadState ts) {
        if (targetMethod == null) {
            long start = Options.startTimer(Options.timeThreadGen);
            PyThreadedCodeGeneratorUSF tcg = new PyThreadedCodeGeneratorUSF(co_name, co_code);
            targetMethod = tcg.generateThreadedCodeTargetMethod();
            Options.stopTimer(Options.timeThreadGen, start, Options.TIMER.THREADGEN);
        }

        return new ActivationRecord(frame, new PyStack(co_stacksize)).execute();
    }

    public class ActivationRecord {
        final PyFrame f;
        final PyStack stack;

        // dummy constructor
        ActivationRecord() {
            this.f = null;
            this.stack = null;
        }

        ActivationRecord(PyFrame frame, PyStack stack) {
            this.f = frame;
            this.stack = stack;
        }

        PyObject execute() {
            return (PyObject) Intrinsics.indirectCallWithReceiver(targetMethod.getEntryPoint(OPTIMIZED_ENTRY_POINT).toAddress(), this);
        }

        public boolean isReenteringImplementation() {
            // the restore stack aspects should occur ONLY after a yield
            if (f.f_savedlocals == null) {
                return false;
            } else {
                for (int i = 0; i < f.f_savedlocals.length; i++) {
                    PyObject v = (PyObject) (f.f_savedlocals[i]);
                    stack.push(v);
                }

                f.f_savedlocals = null;

                Object generatorInput = f.getGeneratorInput();
                if (generatorInput instanceof PyException) {
                    throw (PyException) generatorInput;
                }
                stack.push((PyObject) generatorInput);
                return true;
            }
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.NOP)
        public void nopImplementation() {
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.LOAD_FAST)
        public void loadFastImplementation(int oparg) {
            //print_debug(Opcode.LOAD_FAST, oparg);
            stack.push(f.getlocal(oparg));
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.LOAD_CONST)
        public void loadConstImplementation(int oparg) {
            //print_debug(Opcode.LOAD_CONST, oparg);
            stack.push(co_consts[oparg]);
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.STORE_FAST)
        public void storeFastImplementation(int oparg) {
            //print_debug(Opcode.STORE_FAST, oparg);
            f.setlocal(oparg, stack.pop());
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.POP_TOP)
        public void popTopImplementation() {
            //print_debug(Opcode.POP_TOP, 0);
            stack.pop();
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.ROT_TWO)
        public void rotTwoImplementation() {
            //print_debug(Opcode.ROT_TWO, 0);
            stack.rot2();
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.ROT_THREE)
        public void rotThreeImplementation() {
            //print_debug(Opcode.ROT_THREE, 0);
            stack.rot3();
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.ROT_FOUR)
        public void rotFourImplementation() {
            //print_debug(Opcode.ROT_FOUR, 0);
            stack.rot4();
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.DUP_TOP)
        public void dupTopImplementation() {
            //print_debug(Opcode.DUP_TOP, 0);
            stack.dup();
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.DUP_TOPX)
        public void dupTopXImplementation(int oparg) {
            //print_debug(Opcode.DUP_TOPX, oparg);
            if (oparg == 2 || oparg == 3) {
                stack.dup(oparg);
            } else {
                throw Py.RuntimeError("invalid argument to DUP_TOPX" + " (bytecode corruption?)");
            }
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.UNARY_POSITIVE)
        public void unaryPositiveImplementation() {
            //print_debug(Opcode.UNARY_POSITIVE, 0);
            stack.push(stack.pop().__pos__());
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.UNARY_NEGATIVE)
        public void unaryNegativeImplementation() {
            //print_debug(Opcode.UNARY_NEGATIVE, 0);
            stack.push(stack.pop().__neg__());
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.UNARY_NOT)
        public void unaryNotImplementation() {
            //print_debug(Opcode.UNARY_NOT, 0);
            stack.push(stack.pop().__not__());
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.UNARY_CONVERT)
        public void unaryConvertImplementation() {
            //print_debug(Opcode.UNARY_CONVERT, 0);
            stack.push(stack.pop().__repr__());
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.UNARY_INVERT)
        public void unaryInvertImplementation() {
            //print_debug(Opcode.UNARY_INVERT, 0);
            stack.push(stack.pop().__invert__());
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.BINARY_POWER)
        public void binaryPowerImplementation() {
            //print_debug(Opcode.BINARY_POWER, 0);
            PyObject b = stack.pop();
            PyObject a = stack.pop();
            stack.push(a._pow(b));
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.BINARY_MULTIPLY)
        public void binaryMultiplyImplementation() {
            //print_debug(Opcode.BINARY_MULTIPLY, 0);
            PyObject b = stack.pop();
            PyObject a = stack.pop();
            stack.push(a._mul(b));
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.BINARY_DIVIDE)
        public void binaryDivideImplementation() {
            //print_debug(Opcode.BINARY_DIVIDE, 0);
            PyObject b = stack.pop();
            PyObject a = stack.pop();

            if (!co_flags.isFlagSet(CodeFlag.CO_FUTURE_DIVISION)) {
                stack.push(a._div(b));
            } else {
                stack.push(a._truediv(b));
            }
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.BINARY_TRUE_DIVIDE)
        public void binaryTrueDivideImplementation() {
            //print_debug(Opcode.BINARY_TRUE_DIVIDE, 0);
            PyObject b = stack.pop();
            PyObject a = stack.pop();
            stack.push(a._truediv(b));
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.BINARY_FLOOR_DIVIDE)
        public void binaryFloorDivideImplementation() {
            //print_debug(Opcode.BINARY_FLOOR_DIVIDE, 0);
            PyObject b = stack.pop();
            PyObject a = stack.pop();
            stack.push(a._floordiv(b));
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.BINARY_MODULO)
        public void binaryModuloImplementation() {
            //print_debug(Opcode.BINARY_MODULO, 0);
            PyObject b = stack.pop();
            PyObject a = stack.pop();
            stack.push(a._mod(b));
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.BINARY_ADD)
        public void binaryAddImplementation() {
            //print_debug(Opcode.BINARY_ADD, 0);
            PyObject b = stack.pop();
            PyObject a = stack.pop();
            stack.push(a._add(b));
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.BINARY_SUBTRACT)
        public void binarySubtractImplementation() {
            //print_debug(Opcode.BINARY_SUBTRACT, 0);
            PyObject b = stack.pop();
            PyObject a = stack.pop();
            stack.push(a._sub(b));
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.BINARY_SUBSCR)
        public void binarySubscrImplementation() {
            //print_debug(Opcode.BINARY_SUBSCR, 0);
            PyObject b = stack.pop();
            PyObject a = stack.pop();
            stack.push(a.__getitem__(b));
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.BINARY_LSHIFT)
        public void binaryLshiftImplementation() {
            //print_debug(Opcode.BINARY_LSHIFT, 0);
            PyObject b = stack.pop();
            PyObject a = stack.pop();
            stack.push(a._lshift(b));
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.BINARY_RSHIFT)
        public void binaryRshiftImplementation() {
            //print_debug(Opcode.BINARY_RSHIFT, 0);
            PyObject b = stack.pop();
            PyObject a = stack.pop();
            stack.push(a._rshift(b));
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.BINARY_AND)
        public void binaryAndImplementation() {
            //print_debug(Opcode.BINARY_ADD, 0);
            PyObject b = stack.pop();
            PyObject a = stack.pop();
            stack.push(a._and(b));
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.BINARY_XOR)
        public void binaryXorImplementation() {
            //print_debug(Opcode.BINARY_XOR, 0);
            PyObject b = stack.pop();
            PyObject a = stack.pop();
            stack.push(a._xor(b));
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.BINARY_OR)
        public void binaryOrImplementation() {
            //print_debug(Opcode.BINARY_OR, 0);
            PyObject b = stack.pop();
            PyObject a = stack.pop();
            stack.push(a._or(b));
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.LIST_APPEND)
        public void listAppendImplementation() {
            //print_debug(Opcode.LIST_APPEND, 0);
            PyObject b = stack.pop();
            PyList a = (PyList) (stack.pop());
            a.append(b);
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.INPLACE_POWER)
        public void listInPlacePowerImplementation() {
            //print_debug(Opcode.INPLACE_POWER, 0);
            PyObject b = stack.pop();
            PyObject a = stack.pop();
            stack.push(a._ipow(b));
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.INPLACE_MULTIPLY)
        public void listInPlaceMultiplyImplementation() {
            //print_debug(Opcode.INPLACE_MULTIPLY, 0);
            PyObject b = stack.pop();
            PyObject a = stack.pop();
            stack.push(a._imul(b));
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.INPLACE_DIVIDE)
        public void listInPlaceDivideImplementation() {
            //print_debug(Opcode.INPLACE_DIVIDE, 0);
            PyObject b = stack.pop();
            PyObject a = stack.pop();
            if (!co_flags.isFlagSet(CodeFlag.CO_FUTURE_DIVISION)) {
                stack.push(a._idiv(b));
            } else {
                stack.push(a._itruediv(b));
            }
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.INPLACE_TRUE_DIVIDE)
        public void listInPlaceTrueDivideImplementation() {
            //print_debug(Opcode.INPLACE_TRUE_DIVIDE, 0);
            PyObject b = stack.pop();
            PyObject a = stack.pop();
            stack.push(a._itruediv(b));
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.INPLACE_FLOOR_DIVIDE)
        public void listInPlaceFloorDividePowerImplementation() {
            //print_debug(Opcode.INPLACE_FLOOR_DIVIDE, 0);
            PyObject b = stack.pop();
            PyObject a = stack.pop();
            stack.push(a._ifloordiv(b));
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.INPLACE_MODULO)
        public void listInPlaceModuloImplementation() {
            //print_debug(Opcode.INPLACE_MODULO, 0);
            PyObject b = stack.pop();
            PyObject a = stack.pop();
            stack.push(a._imod(b));
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.INPLACE_ADD)
        public void listInPlaceAddImplementation() {
            //print_debug(Opcode.INPLACE_ADD, 0);
            PyObject b = stack.pop();
            PyObject a = stack.pop();
            stack.push(a._iadd(b));
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.INPLACE_SUBTRACT)
        public void listInPlaceSubtractImplementation() {
            //print_debug(Opcode.INPLACE_SUBTRACT, 0);
            PyObject b = stack.pop();
            PyObject a = stack.pop();
            stack.push(a._isub(b));
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.INPLACE_LSHIFT)
        public void listInPlaceLshiftImplementation() {
            //print_debug(Opcode.INPLACE_LSHIFT, 0);
            PyObject b = stack.pop();
            PyObject a = stack.pop();
            stack.push(a._ilshift(b));
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.INPLACE_RSHIFT)
        public void listInPlaceRshiftImplementation() {
            //print_debug(Opcode.INPLACE_RSHIFT, 0);
            PyObject b = stack.pop();
            PyObject a = stack.pop();
            stack.push(a._irshift(b));
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.INPLACE_AND)
        public void listInPlaceAndImplementation() {
            //print_debug(Opcode.INPLACE_AND, 0);
            PyObject b = stack.pop();
            PyObject a = stack.pop();
            stack.push(a._iand(b));
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.INPLACE_XOR)
        public void listInPlaceXorImplementation() {
            //print_debug(Opcode.INPLACE_XOR, 0);
            PyObject b = stack.pop();
            PyObject a = stack.pop();
            stack.push(a._ixor(b));
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.INPLACE_OR)
        public void listInPlaceOrImplementation() {
            //print_debug(Opcode.INPLACE_OR, 0);
            PyObject b = stack.pop();
            PyObject a = stack.pop();
            stack.push(a._ior(b));
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.SLICE)
        public void sliceImplementation() {
            //print_debug(Opcode.SLICE, 0);
            slice(Opcode.SLICE);
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.SLICE + 1)
        public void slice1Implementation() {
            //print_debug(Opcode.SLICE+1, 0);
            slice(Opcode.SLICE + 1);
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.SLICE + 2)
        public void slice2Implementation() {
            //print_debug(Opcode.SLICE+2, 0);
            slice(Opcode.SLICE + 2);
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.SLICE + 3)
        public void slice3Implementation() {
            //print_debug(Opcode.SLICE+3, 0);
            slice(Opcode.SLICE + 3);
        }

        private void slice(int opcode) {
            PyObject stop = (((opcode - Opcode.SLICE) & 2) != 0) ? stack.pop() : null;
            PyObject start = (((opcode - Opcode.SLICE) & 1) != 0) ? stack.pop() : null;
            PyObject obj = stack.pop();
            stack.push(obj.__getslice__(start, stop));
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.STORE_SLICE)
        public void storeSliceImplementation() {
            //print_debug(Opcode.STORE_SLICE, 0);
            storeSlice(Opcode.STORE_SLICE);
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.STORE_SLICE + 1)
        public void storeSlice1Implementation() {
            //print_debug(Opcode.STORE_SLICE+1, 0);
            storeSlice(Opcode.STORE_SLICE + 1);
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.STORE_SLICE + 2)
        public void storeSlice2Implementation() {
            //print_debug(Opcode.STORE_SLICE+2, 0);
            storeSlice(Opcode.STORE_SLICE + 2);
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.STORE_SLICE + 3)
        public void storeSlice3Implementation() {
            //print_debug(Opcode.STORE_SLICE+3, 0);
            storeSlice(Opcode.STORE_SLICE + 3);
        }

        private void storeSlice(int opcode) {
            PyObject stop = (((opcode - Opcode.STORE_SLICE) & 2) != 0) ? stack.pop() : null;
            PyObject start = (((opcode - Opcode.STORE_SLICE) & 1) != 0) ? stack.pop() : null;
            PyObject obj = stack.pop();
            PyObject value = stack.pop();
            obj.__setslice__(start, stop, value);
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.DELETE_SLICE)
        public void deleteSliceImplementation() {
            //print_debug(Opcode.DELETE_SLICE, 0);
            deleteSlice(Opcode.DELETE_SLICE);
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.DELETE_SLICE + 1)
        public void deleteSlice1Implementation() {
            //print_debug(Opcode.DELETE_SLICE+1, 0);
            deleteSlice(Opcode.DELETE_SLICE + 1);
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.DELETE_SLICE + 2)
        public void deleteSlice2Implementation() {
            //print_debug(Opcode.DELETE_SLICE+2, 0);
            deleteSlice(Opcode.DELETE_SLICE + 2);
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.DELETE_SLICE + 3)
        public void deleteSlice3Implementation() {
            //print_debug(Opcode.DELETE_SLICE+3, 0);
            deleteSlice(Opcode.DELETE_SLICE + 3);
        }

        private void deleteSlice(int opcode) {
            PyObject stop = (((opcode - Opcode.DELETE_SLICE) & 2) != 0) ? stack.pop() : null;
            PyObject start = (((opcode - Opcode.DELETE_SLICE) & 1) != 0) ? stack.pop() : null;
            PyObject obj = stack.pop();
            obj.__delslice__(start, stop);
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.STORE_SUBSCR)
        public void storeSubscrImplementation() {
            //print_debug(Opcode.STORE_SUBSCR, 0);
            PyObject key = stack.pop();
            PyObject obj = stack.pop();
            PyObject value = stack.pop();
            obj.__setitem__(key, value);
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.DELETE_SUBSCR)
        public void deleteSubscrImplementation() {
            //print_debug(Opcode.DELETE_SUBSCR, 0);
            PyObject key = stack.pop();
            PyObject obj = stack.pop();
            obj.__delitem__(key);
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.PRINT_EXPR)
        public void printExprImplementation() {
            //print_debug(Opcode.PRINT_EXPR, 0);
            PySystemState.displayhook(stack.pop());
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.PRINT_ITEM_TO)
        public void printItemToImplementation() {
            //print_debug(Opcode.PRINT_ITEM_TO, 0);
            Py.printComma(stack.pop(), stack.pop());
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.PRINT_ITEM)
        public void printItemImplementation() {
            //print_debug(Opcode.PRINT_ITEM, 0);
            Py.printComma(stack.pop());
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.PRINT_NEWLINE_TO)
        public void printNewLineToImplementation() {
            //print_debug(Opcode.PRINT_NEWLINE_TO, 0);
            Py.printlnv(stack.pop());
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.PRINT_NEWLINE)
        public void printNewLineImplementation() {
            //print_debug(Opcode.PRINT_NEWLINE, 0);
            Py.println();
        }

        /*
         * TODO ADD RAISEVARARGS here
         */
        @PYBYTECODEIMPLEMENTATION(Opcode.LOAD_LOCALS)
        public void loadLocalsImplementation() {
            //print_debug(Opcode.LOAD_LOCALS, 0);
            stack.push(f.f_locals);
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.RETURN_VALUE)
        public PyObject returnValueImplementation() {
            //print_debug(Opcode.RETURN_VALUE, 0);
            PyObject returnValue = stack.pop();

            while (stack.size() > 0) {
                stack.pop();
            }

            // from endOfFunction()
            if (co_flags.isFlagSet(CodeFlag.CO_GENERATOR) && returnValue == Py.None) {
                f.f_lasti = -1;
            }

            return returnValue;
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.YIELD_VALUE)
        public PyObject yieldValueImplementation() {
            //print_debug(Opcode.YIELD_VALUE, 0);
            PyObject returnValue = stack.pop();

            // fromEndofFunction()
            // store the stack in the frame for reentry from the yield;
            f.f_savedlocals = stack.popN(stack.size());
            return returnValue;
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.EXEC_STMT)
        public void execStmtImplementation() {
            //print_debug(Opcode.EXEC_STMT, 0);
            PyObject locals = stack.pop();
            PyObject globals = stack.pop();
            PyObject code = stack.pop();
            Py.exec(code, globals == Py.None ? null : globals, locals == Py.None ? null : locals);
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.POP_BLOCK)
        public void popBlockImplementation() {
            //print_debug(Opcode.POP_BLOCK, 0);
            //PyTryBlock b = popBlock(f);
            PyBlock b = popBlock(f);
            while (stack.size() > b.b_level) {
                stack.pop();
            }
        }

        /*
         * TODO END_FINALLY
         */

        @PYBYTECODEIMPLEMENTATION(Opcode.BUILD_CLASS)
        public void buildClassImplementation() {
            //print_debug(Opcode.BUILD_CLASS, 0);
            PyObject methods = stack.pop();
            PyObject bases[] = ((PySequenceList) (stack.pop())).getArray();
            String name = stack.pop().toString();
            stack.push(Py.makeClass(name, bases, methods));
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.STORE_NAME)
        public void storeNameImplementation(int oparg) {
            //print_debug(Opcode.STORE_NAME, oparg);
            f.setlocal(co_names[oparg], stack.pop());
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.DELETE_NAME)
        public void deleteNameImplementation(int oparg) {
            //print_debug(Opcode.DELETE_NAME, oparg);
            f.dellocal(co_names[oparg]);
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.UNPACK_SEQUENCE)
        public void unpackSequenceImplementation(int oparg) {
            //print_debug(Opcode.UNPACK_SEQUENCE, oparg);
            unpack_iterable(oparg, stack);
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.STORE_ATTR)
        public void storeAttrImplementation(int oparg) {
            //print_debug(Opcode.STORE_ATTR, oparg);
            PyObject obj = stack.pop();
            PyObject v = stack.pop();
            obj.__setattr__(co_names[oparg], v);
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.DELETE_ATTR)
        public void deleteAttrImplementation(int oparg) {
            //print_debug(Opcode.DELETE_ATTR, oparg);
            stack.pop().__delattr__(co_names[oparg]);
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.STORE_GLOBAL)
        public void storeGlobalImplementation(int oparg) {
            //print_debug(Opcode.STORE_GLOBAL, oparg);
            f.setglobal(co_names[oparg], stack.pop());
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.DELETE_GLOBAL)
        public void deleteGlobalImplementation(int oparg) {
            //print_debug(Opcode.DELETE_GLOBAL, oparg);
            f.delglobal(co_names[oparg]);
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.LOAD_NAME)
        public void loadNameImplementation(int oparg) {
            //print_debug(Opcode.LOAD_NAME, oparg);
            stack.push(f.getname(co_names[oparg]));
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.LOAD_GLOBAL)
        public void loadGlobalImplementation(int oparg) {
            //print_debug(Opcode.LOAD_GLOBAL, oparg);
            stack.push(f.getglobal(co_names[oparg]));
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.DELETE_FAST)
        public void deleteFastImplementation(int oparg) {
            //print_debug(Opcode.DELETE_FAST, oparg);
            f.dellocal(oparg);
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.LOAD_CLOSURE)
        public void loadClosureImplementation(int oparg) {
            //print_debug(Opcode.LOAD_CLOSURE, oparg);
            PyCell cell = (PyCell) (f.getclosure(oparg));
            if (cell.ob_ref == null) {
                String name;
                if (oparg >= co_cellvars.length) {
                    name = co_freevars[oparg - co_cellvars.length];
                } else {
                    name = co_cellvars[oparg];
                }
                // XXX - consider some efficient lookup mechanism, like a hash
                // :),
                // at least if co_varnames is much greater than say a certain
                // size (but i would think, it's not going to happen in real
                // code. profile?)
                if (f.f_fastlocals != null) {
                    int i = 0;
                    boolean matched = false;
                    for (String match : co_varnames) {
                        if (match.equals(name)) {
                            matched = true;
                            break;
                        }
                        i++;
                    }
                    if (matched) {
                        cell.ob_ref = f.f_fastlocals[i];
                    }
                } else {
                    cell.ob_ref = f.f_locals.__finditem__(name);
                }
            }
            stack.push(cell);
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.LOAD_DEREF)
        public void loadDeRefImplementation(int oparg) {
            //print_debug(Opcode.LOAD_DEREF, oparg);
            // common code from LOAD_CLOSURE
            PyCell cell = (PyCell) (f.getclosure(oparg));
            if (cell.ob_ref == null) {
                String name;
                if (oparg >= co_cellvars.length) {
                    name = co_freevars[oparg - co_cellvars.length];
                } else {
                    name = co_cellvars[oparg];
                }
                // XXX - consider some efficient lookup mechanism, like a hash
                // :),
                // at least if co_varnames is much greater than say a certain
                // size (but i would think, it's not going to happen in real
                // code. profile?)
                if (f.f_fastlocals != null) {
                    int i = 0;
                    boolean matched = false;
                    for (String match : co_varnames) {
                        if (match.equals(name)) {
                            matched = true;
                            break;
                        }
                        i++;
                    }
                    if (matched) {
                        cell.ob_ref = f.f_fastlocals[i];
                    }
                } else {
                    cell.ob_ref = f.f_locals.__finditem__(name);
                }
            }
            stack.push(cell.ob_ref);
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.STORE_DEREF)
        public void storeDeRefImplementation(int oparg) {
            //print_debug(Opcode.STORE_DEREF, oparg);
            f.setderef(oparg, stack.pop());
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.BUILD_TUPLE)
        public void buildTupleImplementation(int oparg) {
            //print_debug(Opcode.BUILD_TUPLE, oparg);
            stack.push(new PyTuple(stack.popN(oparg)));
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.BUILD_LIST)
        public void buildListImplementation(int oparg) {
            //print_debug(Opcode.BUILD_LIST, oparg);
            stack.push(new PyList(stack.popN(oparg)));
        }

        PyList getPyList_noinline(PyObject[] pyObjects) {
            return new PyList(pyObjects);
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.BUILD_MAP)
        public void buildMapImplementation(int oparg) {
            //print_debug(Opcode.BUILD_MAP, oparg);
            stack.push(new PyDictionary());
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.LOAD_ATTR)
        public void loadAttrImplementation(int oparg) {
            //print_debug(Opcode.LOAD_ATTR, oparg);
            String name = co_names[oparg];
            stack.push(stack.pop().__getattr__(name));
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.COMPARE_LT)
        public void compareLtImplementation() {
            PyObject b = stack.pop();
            PyObject a = stack.pop();
            stack.push(a._lt(b));
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.COMPARE_LE)
        public void compareLeImplementation() {
            PyObject b = stack.pop();
            PyObject a = stack.pop();
            stack.push(a._le(b));
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.COMPARE_EQ)
        public void compareEqImplementation() {
            PyObject b = stack.pop();
            PyObject a = stack.pop();
            stack.push(a._eq(b));
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.COMPARE_NE)
        public void compareNeImplementation() {
            PyObject b = stack.pop();
            PyObject a = stack.pop();
            stack.push(a._ne(b));
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.COMPARE_GT)
        public void compareGtImplementation() {
            PyObject b = stack.pop();
            PyObject a = stack.pop();
            stack.push(a._gt(b));
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.COMPARE_GE)
        public void compareGeImplementation() {
            PyObject b = stack.pop();
            PyObject a = stack.pop();
            stack.push(a._ge(b));
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.COMPARE_IN)
        public void compareInImplementation() {
            PyObject b = stack.pop();
            PyObject a = stack.pop();
            stack.push(a._in(b));
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.COMPARE_NOT_IN)
        public void compareNotInImplementation() {
            PyObject b = stack.pop();
            PyObject a = stack.pop();
            stack.push(a._notin(b));
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.COMPARE_IS)
        public void compareIsImplementation() {
            PyObject b = stack.pop();
            PyObject a = stack.pop();
            stack.push(a._is(b));
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.COMPARE_IS_NOT)
        public void compareIsNotImplementation() {
            PyObject b = stack.pop();
            PyObject a = stack.pop();
            stack.push(a._isnot(b));
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.COMPARE_EXC_MATCH)
        public void compareExcMatchImplementation() {
            PyObject b = stack.pop();
            PyObject a = stack.pop();
            if (a instanceof PyStackException) {
                PyException pye = ((PyStackException) a).exception;
                stack.push(Py.newBoolean(pye.match(b)));
            } else {
                stack.push(Py.newBoolean(new PyException(a).match(b)));
            }
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.COMPARE_OP)
        public void compareOpImplementation(int oparg) {
            //print_debug(Opcode.COMPARE_OP, oparg);
            PyObject b = stack.pop();
            PyObject a = stack.pop();

            switch (oparg) {
                case Opcode.PyCmp_LT:
                    stack.push(a._lt(b));
                    break;
                case Opcode.PyCmp_LE:
                    stack.push(a._le(b));
                    break;
                case Opcode.PyCmp_EQ:
                    stack.push(a._eq(b));
                    break;
                case Opcode.PyCmp_NE:
                    stack.push(a._ne(b));
                    break;
                case Opcode.PyCmp_GT:
                    stack.push(a._gt(b));
                    break;
                case Opcode.PyCmp_GE:
                    stack.push(a._ge(b));
                    break;
                case Opcode.PyCmp_IN:
                    stack.push(a._in(b));
                    break;
                case Opcode.PyCmp_NOT_IN:
                    stack.push(a._notin(b));
                    break;
                case Opcode.PyCmp_IS:
                    stack.push(a._is(b));
                    break;
                case Opcode.PyCmp_IS_NOT:
                    stack.push(a._isnot(b));
                    break;
                case Opcode.PyCmp_EXC_MATCH:
                    if (a instanceof PyStackException) {
                        PyException pye = ((PyStackException) a).exception;
                        stack.push(Py.newBoolean(pye.match(b)));
                    } else {
                        stack.push(Py.newBoolean(new PyException(a).match(b)));
                    }
                    break;
            }
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.IMPORT_NAME)
        public void importNameImplementation(int oparg) {
            //print_debug(Opcode.IMPORT_NAME, oparg);
            PyObject __import__ = f.f_builtins.__finditem__("__import__");
            if (__import__ == null) {
                throw Py.ImportError("__import__ not found");
            }
            PyString name = Py.newString(co_names[oparg]);
            PyObject fromlist = stack.pop();
            PyObject level = stack.pop();

            if (level.asInt() != -1) {
                stack.push(__import__.__call__(new PyObject[] { name, f.f_globals, f.f_locals, fromlist, level }));
            } else {
                 stack.push(__import__.__call__(new PyObject[] { name, f.f_globals, f.f_locals, fromlist }));
            }
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.IMPORT_STAR)
        public void importStarImplementation() {
            //print_debug(Opcode.IMPORT_STAR, 0);
            PyObject module = stack.pop();
            imp.importAll(module, f);
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.IMPORT_FROM)
        public void importFromImplementation(int oparg) {
            //print_debug(Opcode.IMPORT_FROM, oparg);
            String name = co_names[oparg];
            try {
                stack.push(stack.top().__getattr__(name));

            } catch (PyException pye) {
                if (pye.match(Py.AttributeError)) {
                    throw Py.ImportError(String.format(
                            "cannot import name %.230s", name));
                } else {
                    throw pye;
                }
            }
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.JUMP_FORWARD)
        public void jumpForwardImplementation(int oparg) {
            //print_debug(Opcode.JUMP_FORWARD, oparg);
        }

        /*
         * Will return true if it 's going to jump
         */
        @PYBYTECODEIMPLEMENTATION(Opcode.JUMP_IF_FALSE)
        public boolean jumpIfFalseImplementation(int oparg) {
            //print_debug(Opcode.JUMP_IF_FALSE, oparg);
            return !stack.top().__nonzero__();
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.JUMP_IF_TRUE)
        public boolean jumpIfTrueImplementation(int oparg) {
            //print_debug(Opcode.JUMP_IF_TRUE, oparg);
            return stack.top().__nonzero__();
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.JUMP_ABSOLUTE)
        public void jumpAbsoluteImplementation(int oparg) {
            //print_debug(Opcode.JUMP_ABSOLUTE, oparg);
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.GET_ITER)
        public void getIterImplementation() {
            //print_debug(Opcode.GET_ITER, 0);
            PyObject it = stack.top().__iter__();
            if (it != null) {
                stack.set_top(it);
            }
        }

        /*
         * Return true when the iterator is exhausted, i.e. it's the end of the
         * iterator
         */
        @PYBYTECODEIMPLEMENTATION(Opcode.FOR_ITER)
        public boolean forIterImplementation(int oparg) {
            //print_debug(Opcode.FOR_ITER, oparg);
            PyObject it = stack.pop();
            try {
                PyObject x = it.__iternext__();
                if (x != null) {
                    stack.push(it);
                    stack.push(x);
                    return false;
                }
            } catch (PyException pye) {
                if (!pye.match(Py.StopIteration)) {
                    throw pye;
                }
            }
            return true;
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.BREAK_LOOP)
        public void breakLoopImplementation() {
            /**
             * In bytecode interpreter BREAK_LOOP changes the control flow
             * by changing next_instr to b.b_handler.
             * We emit a jump instruction in our threaded code,
             * so here we only adjust the stack size
             */
            //print_debug(Opcode.BREAK_LOOP, 0);
            //PyTryBlock b = popBlock(f);
            PyBlock b = popBlock(f);
            while (stack.size() > b.b_level) {
                stack.pop();
            }
        }

        /* CONTINUE_LOOP bytecode is not used at all
        /*@PYBYTECODEIMPLEMENTATION(Opcode.CONTINUE_LOOP)
        public void continueLoopImplementation(int oparg) {
            //print_debug(Opcode.CONTINUE_LOOP, oparg);
        }*/

        @PYBYTECODEIMPLEMENTATION(Opcode.SETUP_LOOP)
        public void setUpLoopImplementation(int handler) {
            //print_debug(Opcode.SETUP_LOOP, handler);
            //pushBlock(f, new PyTryBlock(Opcode.SETUP_LOOP, handler, stack.size()));
            pushBlock(f, new PyBlock(Opcode.SETUP_LOOP, handler, stack.size()));
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.SETUP_EXCEPT)
        public void setUpExceptImplementation(int oparg) {
            //print_debug(Opcode.SETUP_EXCEPT, oparg);
            // pushBlock(f, new PyTryBlock(opcode, next_instr + oparg,
            // stack.size()));
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.SETUP_FINALLY)
        public void setUpFinallyImplementation(int oparg) {
            //print_debug(Opcode.SETUP_FINALLY, oparg);
            // pushBlock(f, new PyTryBlock(opcode, next_instr + oparg,
            // stack.size()));
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.WITH_CLEANUP)
        public void withCleanUpImplementation() {
            //print_debug(Opcode.WITH_CLEANUP, 0);
            /*
             * TOP is the context.__exit__ bound method. Below that are 1-3
             * values indicating how/why we entered the finally clause: - SECOND
             * = None - (SECOND, THIRD) = (WHY_{RETURN,CONTINUE}), retval -
             * SECOND = WHY_*; no retval below it - (SECOND, THIRD, FOURTH) =
             * exc_info() In the last case, we must call TOP(SECOND, THIRD,
             * FOURTH) otherwise we must call TOP(None, None, None)
             *
             * In addition, if the stack represents an exception,and* the
             * function call returns a 'true' value, we "zap" this information,
             * to prevent END_FINALLY from re-raising the exception. (But
             * non-local gotos should still be resumed.)
             */
            PyObject exit = stack.top();
            PyObject u = stack.top(2);
            PyObject v;
            PyObject w;
            if (u == Py.None || u instanceof PyStackWhy) {
                u = v = w = Py.None;
            } else {
                v = stack.top(3);
                w = stack.top(4);
            }
            PyObject x = null;
            if (u instanceof PyStackException) {
                PyException exc = ((PyStackException) u).exception;
                x = exit.__call__(exc.type, exc.value, exc.traceback);
            } else {
                x = exit.__call__(u, v, w);
            }

            if (u != Py.None && x != null && x.__nonzero__()) {
                stack.popN(4); // XXX - consider stack.stackadj op
                stack.push(Py.None);
            } else {
                stack.pop(); // this should be popping off a block
            }
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.CALL_FUNCTION)
        public void callFunctionImplementation(int oparg) {
            //print_debug(Opcode.CALL_FUNCTION, oparg);
            int na = oparg & 0xff;
            int nk = (oparg >> 8) & 0xff;

            if (nk == 0) {
                call_function(na, stack);
            } else {
                call_function(na, nk, stack);
            }
        }

         /* @PYTHONBYTECODEImplementation(Opcode.CALL_FUNCTION_VAR)
           public void callFunctionVarImplementation(int oparg) {
             callFunctionVar(Opcode.CALL_FUNCTION_VAR, oparg);
           }

           @PYTHONBYTECODEImplementation(Opcode.CALL_FUNCTION_KW)
           public void callFunctionKwImplementation(int oparg) {
             callFunctionVar(Opcode.CALL_FUNCTION_KW, oparg);
           }

           @PYTHONBYTECODEImplementation(Opcode.CALL_FUNCTION_VAR_KW)
           public void
             callFunctionVarKwImplementation(int oparg) {
             callFunctionVar(Opcode.CALL_FUNCTION_VAR_KW, oparg);
           }
           private void callFunctionVar(int opcode, int oparg) {
             print_debug(Opcode.CALL_FUNCTION_VAR, oparg);
              * int na = oparg &
              * 0xff; int nk = (oparg >> 8) & 0xff; int flags = (opcode -
              * Opcode.CALL_FUNCTION) & 3; call_function(na, nk, (flags &
              * CALL_FLAG_VAR) != 0, (flags & CALL_FLAG_KW) != 0, stack); }
         */

        @PYBYTECODEIMPLEMENTATION(Opcode.MAKE_FUNCTION)
        public void makeFunctionImplementation(int oparg) {
            //print_debug(Opcode.MAKE_FUNCTION, oparg);
            PyCode code = (PyCode) stack.pop();
            PyObject[] defaults = stack.popN(oparg);
            PyObject doc = null;
            if (code instanceof PyBytecode && ((PyBytecode) code).co_consts.length > 0) {
                doc = ((PyBytecode) code).co_consts[0];
            }
            PyFunction func = new PyFunction(f.f_globals, defaults, code, doc);
            stack.push(func);
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.MAKE_CLOSURE)
        public void makeClosureImplementation(int oparg) {
            //print_debug(Opcode.MAKE_CLOSURE, oparg);
            PyCode code = (PyCode) stack.pop();
            PyObject[] closure_cells = ((PySequenceList) (stack.pop())).getArray();
            PyObject[] defaults = stack.popN(oparg);
            PyObject doc = null;
            if (code instanceof PyBytecode && ((PyBytecode) code).co_consts.length > 0) {
                doc = ((PyBytecode) code).co_consts[0];
            }
            PyFunction func = new PyFunction(f.f_globals, defaults, code, doc, closure_cells);
            stack.push(func);
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.BUILD_SLICE)
        public void buildSliceImplementation(int oparg) {
            //print_debug(Opcode.BUILD_SLICE, oparg);
            PyObject step = oparg == 3 ? stack.pop() : null;
            PyObject stop = stack.pop();
            PyObject start = stack.pop();
            stack.push(new PySlice(start, stop, step));
        }

        @PYBYTECODEIMPLEMENTATION(Opcode.EXTENDED_ARG)
        public void extendedArgImplementation(int oparg) {
            //print_debug(Opcode.EXTENDED_ARG, oparg);
        }

        void print_debug(int opcode, int oparg) {
            //if (debug) {
                /*System.err.println(co_name + " "
                        + get_opname().__getitem__(Py.newInteger(opcode))
                        + " opcode " + opcode + " "
                        + (opcode >= Opcode.HAVE_ARGUMENT ? " " + oparg : "")
                        + ", stack: " + stack.toString());
                */
            //}
        }
    }
}
