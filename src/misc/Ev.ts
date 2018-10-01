export class Ev <V> {
    protected callbacks: Set<(value: V) => void>;
    constructor () {
        this.callbacks = new Set();
    }
    addListener (callback: (value: V) => void) {
        this.callbacks.add(callback);
    }
    removeListener (callback: (value: V) => void) {
        this.callbacks.delete(callback);
    }
    dispatch (value: V) {
        for (const callback of this.callbacks) {
            callback(value);
        }
    }
    filter <V2 = any> (predicate: (value: V) => boolean) {
        return new EvFiltered<V, V2>(this, predicate);
    }
    map <V2 = any> (mapper: (value: V) => V2) {
        return new EvMapped<V, V2>(this, mapper);
    }
}

class EvFiltered <V1, V2> extends Ev<V2> {
    protected parent: Ev<V1>;
    protected parentCallback: (value: V1) => void;
    constructor (parent: Ev<V1>, predicate: (value: V1) => any) {
        super();
        this.parent = parent;
        this.parentCallback = (v: V1) => {
            if (predicate(<any> v)) {
                this.dispatch(<V2> <any> v);
            }
        };
    }
    addListener (callback: (value: V2) => void) {
        if (this.callbacks.size === 0) {
            this.parent.addListener(this.parentCallback);
        }
        super.addListener(callback);
    }
    removeListener (callback: (value: V2) => void) {
        super.removeListener(callback);
        if (this.callbacks.size === 0) {
            this.parent.removeListener(this.parentCallback);
        }
    }
}

class EvMapped <V1, V2> extends Ev<V2> {
    protected parent: Ev<V1>;
    protected parentCallback: (value: V1) => void;
    constructor (parent: Ev<V1>, mapper: (value: V1) => V2) {
        super();
        this.parent = parent;
        this.parentCallback = (v: V1) => {
            this.dispatch(mapper(<any> v));
        };
    }
    addListener (callback: (value: V2) => void) {
        if (this.callbacks.size === 0) {
            this.parent.addListener(this.parentCallback);
        }
        super.addListener(callback);
    }
    removeListener (callback: (value: V2) => void) {
        super.removeListener(callback);
        if (this.callbacks.size === 0) {
            this.parent.removeListener(this.parentCallback);
        }
    }
}
