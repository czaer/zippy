package org.python.modules.truffle;

import java.util.Arrays;
import java.util.Collections;
import java.util.List;

import org.python.ast.datatypes.PList;
import org.python.modules.truffle.annotations.ModuleMethod;

public class ListAttribute extends Module{
    
    public ListAttribute() {
        try {
            addAttributeMethods();
        } catch (NoSuchMethodException | SecurityException e) {
            // TODO Auto-generated catch block
            e.printStackTrace();
        }
    }
    
    
    @ModuleMethod
    public PList append(Object[] args, Object self) {        
        if (args.length == 1) {
            return append(args[0], self);
        } else {
            throw new RuntimeException("wrong number of arguments for append()");
        }
    }
    
    public PList append(Object arg, Object self) {
        PList selfList = (PList) self;
        
        selfList.getList().add(arg);
        return selfList;
    }
    
    public PList append(Object arg0, Object arg1, Object self) {        
        throw new RuntimeException("wrong number of arguments for append()");
    }

    @ModuleMethod
    public PList extend(Object[] args, Object self) {        
        if (args.length == 1) {
           return extend(args[0], self);
        } else {
            throw new RuntimeException("wrong number of arguments for extend()");
        }
    }
    
    public PList extend(Object arg, Object self) {
        PList selfList = (PList) self;
        
        if (arg instanceof PList) {
            List<Object> list = ((PList) arg).getList();
            for (int i = 0; i < list.size(); i++)
                selfList.getList().add(list.get(i));
            return selfList;
        } else {
            throw new RuntimeException("invalid arguments for extend()");
        }
    }
    
    public PList extend(Object arg0, Object arg1, Object self) {        
        throw new RuntimeException("wrong number of arguments for extend()");
    }

    @ModuleMethod
    public PList insert(Object[] args, Object self) {
        if (args.length == 2) {
            return insert(args[0], args[1], self);
        } else {
            throw new RuntimeException("wrong number of arguments for insert()");
        }
    }
    
    public PList insert(Object arg0, Object arg1, Object self) {
        PList selfList = (PList) self;

        if (arg0 instanceof Integer) {
            selfList.getList().add((int) arg0, arg1);
            return selfList;
        } else {
            throw new RuntimeException("invalid arguments for insert()");
        }
    }
    
    public PList insert(Object arg, Object self) {
        throw new RuntimeException("wrong number of arguments for insert()");
    }

    @ModuleMethod
    public PList remove(Object[] args, Object self) {
        if (args.length == 1) {
            return remove(args[0], self);
        } else {
            throw new RuntimeException("wrong number of arguments for remove()");
        }
    }
    
    public PList remove(Object arg, Object self) {
        PList selfList = (PList) self;
        
        if (selfList.getList().remove(arg))
            return selfList;
        else
            throw new RuntimeException("invalid arguments for remove()");
    }
    
    public PList remove(Object arg0, Object arg1, Object self) {
        throw new RuntimeException("wrong number of arguments for remove()");
    }

    @ModuleMethod
    public Object pop(Object[] args, Object self) {
        PList selfList = (PList) self;

        if (args.length == 0) {
            Object ret = selfList.getList().get(selfList.getList().size() - 1);
            selfList.getList().remove(selfList.getList().size() - 1);
            return ret;
        } else if (args.length == 1) {
            return pop(args[0], self);
        } else {
            throw new RuntimeException("wrong number of arguments for pop()");
        }
    }
    
    public Object pop(Object arg, Object self) {
        PList selfList = (PList) self;

        if (arg instanceof Integer) {
            int index = (int) arg;
            Object ret = selfList.getList().get(index);
            selfList.getList().remove(index);
            return ret;
        } else {
            throw new RuntimeException("invalid arguments for pop()");
        }
    }
    
    public Object pop(Object arg0, Object arg1, Object self) {
        throw new RuntimeException("wrong number of arguments for pop()");
    }

    @ModuleMethod
    public int index(Object[] args, Object self) {
        if (args.length == 1) {
           return index(args[0], self);
        } else {
            throw new RuntimeException("wrong number of arguments for index()");
        }
    }
    
    public int index(Object arg, Object self) {
        PList selfList = (PList) self;

        int ret = selfList.getList().indexOf(arg);
        if (ret != -1)
            return ret;
        else
            throw new RuntimeException("invalid arguments for index()");
    }
    
    public int index(Object arg0, Object arg1, Object self) {
        throw new RuntimeException("wrong number of arguments for index()");
    }

    @ModuleMethod
    public int count(Object[] args, Object self) {
        if (args.length == 1) {
            return count(args[0], self);
        } else {
            throw new RuntimeException("wrong number of arguments for count()");
        }
    }
    
    public int count(Object arg, Object self) {
        PList selfList = (PList) self;

        int ret = 0;
        List<Object> list = selfList.getList();
        for (int i = 0; i < list.size(); i++) {
            if (list.get(i).equals(arg))
                ret++;
        }
        return ret;
    }
    
    public int count(Object arg0, Object arg1, Object self) {
        throw new RuntimeException("wrong number of arguments for count()");
    }

    @ModuleMethod
    public PList sort(Object args[], Object self) {
        PList selfList = (PList) self;

        if (args.length == 0) {
            Object[] sorted = selfList.getList().toArray();
            Arrays.sort(sorted);
            int index = 0;
            for (int i = 0; i < sorted.length; i++) {
                selfList.getList().set(index++, sorted[i]);
            }
            return selfList;
        } else {
            throw new RuntimeException("wrong number of arguments for sort()");
        }
    }
    
    public PList sort(Object arg, Object self) {
        throw new RuntimeException("wrong number of arguments for sort()");
    }
    
    public PList sort(Object arg0, Object arg1, Object self) {
        throw new RuntimeException("wrong number of arguments for sort()");
    }

    @ModuleMethod
    public PList reverse(Object[] args, Object self) {
        PList selfList = (PList) self;

        if (args.length == 0) {
            Collections.reverse(selfList.getList());
            return selfList;
        } else {
            throw new RuntimeException("wrong number of arguments for reverse()");
        }
    }
    
    public PList reverse(Object arg, Object self) {
        throw new RuntimeException("wrong number of arguments for reverse()");
    }
    
    public PList reverse(Object arg0, Object arg1, Object self) {
        throw new RuntimeException("wrong number of arguments for reverse()");
    }
}