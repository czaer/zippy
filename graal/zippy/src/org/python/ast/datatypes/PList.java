package org.python.ast.datatypes;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.Iterator;
import java.util.List;

import org.python.util.Generic;
import org.python.modules.truffle.*;

public class PList extends PSequence {

    private final List<Object> list;
    
    static final public ListAttribute listModule = new ListAttribute();

    public PList() {
        list = new ArrayList<Object>();
    }

    public PList(Object[] elements) {
        list = new ArrayList<Object>(Arrays.asList(elements));
    }

    public PList(Iterable<?> iterable) {
        list = new ArrayList<Object>();
        for (Object o : iterable) {
            addItem(o);
        }
    }

    public PList(List<Object> list, boolean convert) {
        if (!convert) {
            this.list = list;
        } else {
            this.list = Generic.list(); // TODO make list of CollectionElement
            for (int i = 0; i < list.size(); i++) {
                addItem(list.get(i));
            }
        }
    }

    public List<Object> getList() {
        return list;
    }

    @Override
    public Object[] getSequence() {
        return list.toArray();
    }

    @Override
    public int len() {
        return list.size();
    }

    @Override
    public Object getItem(int idx) {
        if (idx < 0) {
            idx += list.size();
        }
        return list.get(idx);
    }

    @Override
    public void setItem(int idx, Object value) {
        if (idx < 0) {
            idx += list.size();
        }
        list.set(idx, value);
    }

    @Override
    public Object getSlice(PSlice slice) {
        int length = slice.computeActualIndices(list.size());
        return getSlice(slice.getStart(), slice.getStop(), slice.getStep(), length);
    }

    @Override
    public Object getSlice(int start, int stop, int step, int length) {
        List<Object> newList;
        if (step == 1) {
            newList = new ArrayList<Object>(list.subList(start, stop));
        } else {
            newList = new ArrayList<Object>(length);
            for (int i = start, j = 0; j < length; i += step, j++) {
                newList.add(list.get(i));
            }
        }
        return new PList(newList, false);
    }

    @Override
    public void setSlice(PSlice slice, PSequence value) {
        setSlice(slice.getStart(), slice.getStop(), slice.getStep(), value);
    }

    /**
     * Set slice.
     * 
     * @param start
     * @param stop
     * @param step
     * @param value
     *            the value can only be a TruffleSequence or a java.util.List
     */
    @Override
    public void setSlice(int start, int stop, int step, PSequence value) {

        if (start == Integer.MIN_VALUE) {
            start = step < 0 ? list.size() - 1 : 0;
        }
        if (stop == Integer.MIN_VALUE) {
            stop = step < 0 ? -1 : list.size();
        }

        if (stop < start) {
            stop = start;
        }

        setsliceIterator(start, stop, step, value.iterator());
    }

    final private void setsliceIterator(int start, int stop, int step, Iterator<Object> iter) {
        if (step == 1) {
            List<Object> insertion = new ArrayList<Object>();
            if (iter != null) {
                while (iter.hasNext()) {
                    insertion.add(iter.next());
                }
            }

            list.subList(start, stop).clear();
            list.addAll(start, insertion);
        } else {
            int size = list.size();
            for (int j = start; iter.hasNext(); j += step) {
                Object item = iter.next();

                if (j >= size) {
                    list.add(item);
                } else {
                    list.set(j, item);
                }
            }
        }
    }

    @Override
    public void delItem(int idx) {
        list.remove(idx);
    }

    @Override
    public void delItems(int start, int stop) {
        list.subList(start, stop).clear();
    }

    public void addItem(int index, Object element) {
        list.add(index, element);
    }

    public boolean addItem(Object o) {
        return list.add(o);
    }

    @Override
    public Iterator<Object> iterator() {
        return list.iterator();
    }

    @Override
    public boolean lessThan(PSequence sequence) {
        return false;
    }

    public PCallable findAttribute(String name) {
        PCallable method = listModule.lookupMethod(name);
        method.setSelf(this);
        return method;
    }

    @Override
    public String toString() {
        StringBuilder buf = new StringBuilder("[");
        int length = list.size();
        int i = 0;

        for (Object item : list) {
            buf.append(item.toString());

            if (i < length - 1) {
                buf.append(", ");
            }

            i++;
        }

        buf.append("]");
        return buf.toString();
    }

    @Override
    public Object getMin() {
        Object[] copy = this.list.toArray();
        Arrays.sort(copy);
        return copy[0];
    }

    @Override
    public Object getMax() {
        Object[] copy = this.list.toArray();
        Arrays.sort(copy);
        return copy[copy.length - 1];
    }
    
    @Override
    public Object multiply(int value) {
        ArrayList<Object> result = new ArrayList<Object>();
        for (int i = 0; i < value; i++) {
            for (int j = 0; j < list.size(); j++) {
                result.add(list.get(j));
            }
        }
        
        return new PList(result);
    }

}
