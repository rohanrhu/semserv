# semserv
High-performance async semaphore service useable with long string ipc keys stored in memory.

#### Build

Compile and run semserv server with
`make && ./build/semserv`

#### Example

Run client with `node`

###### Client 1

```javascript
> require('test/test.js')
> semserv('key1', 1/*acquire*/)
```

###### Client 2

```javascript
> require('test/test.js')
> semserv('key1', 1)
> semserv('key1', 2/*release*/)
> semserv('key1', 2)
```

#### Contributing

Patches welcome.

(Node.js is good for playground.)

#### Todo

* PHP Adapter
* Node.js Adapter
