/* global initSqlJs */
/* eslint-env worker */
/* eslint no-restricted-globals: ["error"] */

"use strict";

var _dbList = {};

function onModuleReady(SQL, event) {
	function getDB(name, data) {
		if (_dbList[name] != null) return _dbList[name];
		try {
			let sql = new SQL.Database(name, data);
			let res = sql.exec(`
				PRAGMA user_version;
				PRAGMA journal_mode=MEMORY;
				PRAGMA page_size=8192;
			`);
			if(!data) {
				let init = true;
				if(res.length && res[0].values.length) {
					if(res[0].values[0][0] === 3)
						init = false;
				}
				if(init) {
					sql.exec(`CREATE TABLE IF NOT EXISTS info (
								name text NOT NULL UNIQUE,
								version INTEGER NOT NULL,
								rev INTEGER NOT NULL,
								extra BLOB
							);

							CREATE TABLE IF NOT EXISTS notes (
								id INTEGER PRIMARY KEY,
								title text NOT NULL,
								tags TEXT DEFAULT "",
								creator TEXT DEFAULT "",
								created INTEGER DEFAULT 0,
								modified INTEGER DEFAULT 0,
								modifier TEXT DEFAULT "",
								revision INTEGER DEFAULT 0,
								fields BLOB DEFAULT ""
							);

							CREATE TABLE IF NOT EXISTS extrafields (
								id INTEGER PRIMARY KEY,
								noteId INTEGER NOT NULL,
								name text NOT NULL,
								value text,
								FOREIGN KEY(noteId) REFERENCES notes(id) ON UPDATE CASCADE ON DELETE CASCADE
							);

							CREATE UNIQUE INDEX IF NOT EXISTS titleIndex ON notes(title);
							CREATE INDEX IF NOT EXISTS noteIndex ON extrafields(noteId);

							REPLACE INTO info(rowid, name, version, rev) VALUES (0,'info',1, 0);

							PRAGMA user_version=3;
					`);
				}
			}
			_dbList[name] = sql;
			return _dbList[name];
		}
		catch(err) {
			return err;
		}

	}

	var buff; var data; var result;
	data = event.data;

	var cmds = data.commands;
	var size = cmds.length;

	for(var i=0; i<size; ++i) {
		var cmd = cmds[i];

		if(!cmd.name) {
			throw "command: Missing name string";
		}

		var db = null;
		var res = {};

    	switch(cmd.action) {
			case "open":
				buff = cmd.buffer;

				if(_dbList[cmd.name]) {
					_dbList[cmd.name].close(buff ? true : false);
					_dbList[cmd.name] = null;
				}

				db = getDB(cmd.name, buff && new Uint8Array(buff));

				postMessage({
					id: cmd.id,
					index: cmd.index,
//					buffer: cmd.buffer || null,
					extra: cmd.extra,
					error: typeof db === "string" ? db : null
//				}, cmd.buffer ? [cmd.buffer] : null);
				});

				break;

			case "exec":
				if(!cmd.sql) {
					throw "exec: Missing query string";
				}

				db = getDB(cmd.name);

				res = {
					id: cmd.id,
					index: cmd.index,
					extra: cmd.extra
				};

				try {
					res.results = db.exec(cmd.sql, cmd.params);
					postMessage(res);
				}
				catch(err) {
					postMessage({error: err, commands: [res]});
				}

				break;

			case "each":
				db = getDB(cmd.name);

				var callback = function(row) {
					return postMessage({
						id: cmd.id,
                    	index: cmd.index,
						extra: cmd.extra,
						row: row,
						finished: false
					});
				};
				var done = function() {
					return postMessage({
						id: data.id,
						index: cmd.index,
						extra: cmd.extra,
						finished: true
					});
				};

				db.each(cmd.sql, cmd.params, callback, done);

				break;

			case "export":
				db = getDB(cmd.name);
				buff = db.export();
				result = {
				id: cmd.id,
					index: cmd.index,
					version: cmd.version,
					extra: cmd.extra,
					buffer: buff.buffer || buff
				};
				try {
					postMessage(result, [buff.buffer || buff]);
				} catch (error) {
					postMessage(result);
				}

				break;

			case "close":
				if(_dbList[cmd.name]) {
					_dbList[cmd.name].close();
					_dbList[cmd.name] = null;
				}

				postMessage({
					id: cmd.id,
					extra: cmd.extra,
					index: cmd.index
				});

				break;

			case "searchfilter":
				var filter;
				var msg;
				db = getDB(cmd.name);
				if(db) {
					try {
						filter = SQL.FullTextSearch.createSearchFilter("en", cmd.text);
					}
					catch(err) {
						msg = err;
					}
				}
				postMessage({id: "sync", results: filter.slice(), index: cmd.index, extra: cmd.extra, error: msg})

				break;

			default:
				throw new Error("Invalid action : " + cmd.action);
		}

	}
}

function onError(err) {
	return postMessage({
		commands: this.data.commands,
//		id: this["data"]["id"],
//		index: this["data"]["index"],
//		extra: this.data.extra,
		error: err["message"]
	});
}

if (typeof importScripts === "function") {
	var sqlModuleReady = initSqlJs();
	var init = false;

	self.onmessage = function onmessage(event) {
		return sqlModuleReady
			.then((sql) => {
				if(!init) {
					db = null;
					let sqlFS = new SQLiteFS(sql.FS, new IndexedDBBackend());
					sql.register_for_idb(sqlFS);

					sql.FS.mkdir('/sql');
					sql.FS.mount(sqlFS, {}, '/sql');
					init = true;
				}

				return sql;

			})
			.then((sql) => onModuleReady(sql, event))
			.catch(onError.bind(event));
	};
}
