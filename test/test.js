
var PACKET_SIGNATURE = 2015;
var PACKET_SIGNATURE_LEN = 2;

var PACKET_CMD_ACQUIRE = 1;
var PACKET_CMD_RELEASE = 2;

var SEM_STATE_LOCKED = 1;
var SEM_STATE_AVAILABLE = 2;

var net = require('net');

var socket = net.connect({
    host: '127.0.0.1',
    port: '5001'
}, function () {
    
});

var data_buf = new Buffer(0);

socket.on('data', function (data) {
    data_buf = Buffer.concat([data_buf, new Buffer(data)]);
    
    if (data_buf.length < 7) {
        return;
    }

    var signature = data_buf.readUInt16LE(0);
    var state = data_buf.readUInt8(2);
    var key_len = data_buf.readUInt32LE(3);

    if (data_buf.length < (7 + key_len)) {
        return;
    }

    var key = data_buf.slice(7, data_buf.length).toString();

    console.log('RESPONSE: state='+state+', key_len='+key_len+', key='+key);

    if (state == SEM_STATE_AVAILABLE)  {
        console.log('RESPONSE: CONTINUE');
    }
});

var semserv = function (key, cmd) {
    var message = new Buffer(key);
    var buffer = new Buffer(6);
    buffer.writeUInt16LE(PACKET_SIGNATURE, 0);
    buffer.writeUInt32LE(message.length, 2);
    buffer = Buffer.concat([buffer, message]);
    buffer = Buffer.concat([buffer, new Buffer([cmd])]);
    socket.write(buffer);
}

var key = 'news-5-category-10-'+parseInt(Math.random()*10000);

console.log('acquiring semaphore.. waiting for release..');
semserv(key, PACKET_CMD_ACQUIRE);
setTimeout(function () {
    console.log('acquiring semaphore.. waiting for release..');
    semserv(key, PACKET_CMD_ACQUIRE);
}, 1000);
setTimeout(function () {
    console.log('releasing semaphore..');
    semserv(key, PACKET_CMD_RELEASE);
}, 3000);