// Installed before the generated runtime; invoked after its filesystem exists.
Module['kbInitializeStorage'] = function() {
    const root = '/21kb-user-' + encodeURIComponent(new URL('.', location.href).pathname);
    const names = ['clean', 'dirty', 'pending', 'failed', 'unavailable'];
    const storage = Module['kbUserStorage'] = {
        root, startup:0, status:2, diagnostic:'', revision:0, clock:Date.now(), blocked:false,
        publish(status, diagnostic='') {
            this.status = status;
            this.diagnostic = diagnostic;
            document.documentElement.dataset.kbStorage = names[status];
        },
        changed(path) {
            ++this.revision;
            // IDBFS reconciles modification times. Preserve distinct writes
            // even within one browser clock tick or after a clock adjustment.
            this.clock = Math.max(Date.now(), this.clock + 1);
            if (path) FS.utime(path, this.clock, this.clock);
            if (this.status !== 2) this.publish(1);
        },
        flush() {
            if (this.startup !== 1 || this.blocked) return false;
            if (this.status === 2 || this.status === 0) return true;
            const revision = this.revision;
            this.publish(2);
            const complete = error => {
                if (error) this.publish(3, String(error.name || 'StorageError') + ': ' + String(error.message || error));
                else this.publish(this.revision === revision ? 0 : 1);
            };
            this.sync(false, complete);
            return true;
        },
        sync(populate, callback) {
            let settled = false;
            const complete = error => {
                if (settled) return;
                settled = true;
                clearTimeout(timer);
                this.pendingCompletion = null;
                callback(error);
            };
            const timer = setTimeout(() => {
                // An unconfirmed operation may still be running. Do not
                // overlap a retry or acknowledge a late callback as success.
                this.blocked = true;
                complete(new DOMException('Storage synchronization timed out; reload before retrying', 'TimeoutError'));
            }, 15000);
            this.pendingCompletion = complete;
            try { FS.syncfs(populate, complete); }
            catch (error) { complete(error); }
        }
    };
    storage.publish(2);
    const fail = error => {
        storage.startup = -1;
        storage.publish(3, String(error.name || 'StorageError') + ': ' + String(error.message || error));
    };
    try {
        FS.mkdir(root);
        FS.mount(IDBFS, {}, root);
        // Observe the actual owned database as well as the SDK callback:
        // an explicit transaction.abort() can have a null transaction.error.
        IDBFS.getDB(root, (error, database) => {
            if (error) { fail(error); return; }
            database.addEventListener('abort', event => {
                storage.pendingCompletion?.(event.target.error ||
                    new DOMException('Browser storage transaction was aborted', 'AbortError'));
            });
            storage.sync(true, error => {
            if (error) { fail(error); return; }
            try {
                const visit = path => {
                    const info = FS.stat(path);
                    storage.clock = Math.max(storage.clock, Number(info.mtime));
                    if (FS.isDir(info.mode)) {
                        for (const name of FS.readdir(path)) {
                            if (name !== '.' && name !== '..') visit(path + '/' + name);
                        }
                    }
                };
                visit(root);
                storage.startup = 1;
                storage.publish(0);
            } catch (error) { fail(error); }
            });
        });
    } catch (error) { fail(error); }
};
