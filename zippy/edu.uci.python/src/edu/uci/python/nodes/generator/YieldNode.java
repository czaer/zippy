/*
 * Copyright (c) 2013, Regents of the University of California
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
package edu.uci.python.nodes.generator;

import com.oracle.truffle.api.frame.*;

import edu.uci.python.ast.VisitorIF;
import edu.uci.python.nodes.*;
import edu.uci.python.nodes.statement.*;
import edu.uci.python.runtime.exception.*;
import static edu.uci.python.nodes.generator.GeneratorBlockNode.*;

public class YieldNode extends StatementNode {

    @Child protected PNode right;
    private final int parentBlockIndexSlot;

    public YieldNode(PNode right) {
        this.right = right;
        parentBlockIndexSlot = -1; // initial value to be replaced with a valid index.
    }

    public YieldNode(YieldNode prev, int parentBlockIndexSlot) {
        this.right = prev.right;
        this.parentBlockIndexSlot = parentBlockIndexSlot;
    }

    public final int getParentBlockIndexSlot() {
        return parentBlockIndexSlot;
    }

    public PNode getRhs() {
        return right;
    }

    @Override
    public Object execute(VirtualFrame frame) {
        right.execute(frame);
        assert parentBlockIndexSlot != -1;
        final int index = getIndex(frame, parentBlockIndexSlot);
        setIndex(frame, parentBlockIndexSlot, index + 1);
        throw YieldException.INSTANCE;
    }

    @Override
    public <R> R accept(VisitorIF<R> visitor) throws Exception {
        return visitor.visitYieldNode(this);
    }

}
