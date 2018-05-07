import child_process from "child_process"
import express from "express"
import httpProxy from "http-proxy"
import Path from "path"

export class WebxEngineRemote {
  name: string = "(remote)"

  ENGINE_ROOT: string
  APP_ROOT: string
  config: Object

  route() {
    const router = express.Router()
    router.use(this.dispatch.bind(this))
    return router
  }
  getName() {
    return this.name
  }
  dispatch(req, res) {
    if (this.proxy) this.proxy.web(req, res)
    else res.status(404).send("Application not online")
  }
  connect(ENGINE_ROOT: string, APP_ROOT: string, config: Object): Promise {
    if (this.child) throw new Error("Webx engine already connected")
    this.ENGINE_ROOT = ENGINE_ROOT
    this.APP_ROOT = APP_ROOT
    this.config = config

    this.child = child_process.fork(Path.dirname(__filename) + "../../../dist/lib/server_child.js", [], {
      cwd: process.cwd(),
      stdio: [0, 1, 2, "ipc"],
      //execArgv: ["--inspect"],
      execArgv: [],
    })

    this.child.on("message", this._ipc.bind(this))

    this.child.send({
      type: "webx-connect",
      ENGINE_ROOT: this.ENGINE_ROOT,
      APP_ROOT: this.APP_ROOT,
      config: this.config,
    })
  }
  disconnect() {
    if (!this.child) throw new Error("Webx engine not connected")
    this.child.kill()
    this.child = null
    this.proxy = null
  }
  _ipc_webx_listen(msg) {
    console.log(msg)
    this.name = msg.engine
    this.proxy = new httpProxy.createProxyServer({
      target: {
        host: msg.host,//'localhost',
        port: msg.port,//9015
      }
    });
  }
  _ipc(msg) {
    console.log(" > parent:", msg.type, msg.id !== undefined ? msg.id : "")
    switch (msg.type) {
      case "webx-listen":
        return this._ipc_webx_listen(msg)
      default:
        console.log("IPC receive unsupported messsage with type: ", msg.type)
    }
  }
}
