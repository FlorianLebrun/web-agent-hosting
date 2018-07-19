#include "./WebxHttpTransaction.h"

static std::atomic<intptr_t> leakcount = 0;

WebxHttpTransaction::WebxHttpTransaction(
  v8::Local<v8::Object> req,
  v8::Local<v8::Function> onBegin,
  v8::Local<v8::Function> onWrite,
  v8::Local<v8::Function> onEnd)
  : response(this)
{
  using namespace v8;
  this->onBegin.Reset(Isolate::GetCurrent(), onBegin);
  this->onWrite.Reset(Isolate::GetCurrent(), onWrite);
  this->onEnd.Reset(Isolate::GetCurrent(), onEnd);

  // Set request pseudo headers
  this->setAttributStringV8(":method", v8h::GetIn(req, "method"));
  this->setAttributStringV8(":path", v8h::GetIn(req, "url"));
  this->setAttributStringV8(":scheme", v8h::GetIn(req, "protocole"));
  this->setAttributStringV8(":authority", v8h::GetIn(req, "hostname"));
  this->setAttributStringV8(":original-path", v8h::GetIn(req, "originalUrl"));

  // Set request headers
  Local<Object> headers = v8h::GetIn(req, "headers")->ToObject();
  Local<Array> keys = headers->GetOwnPropertyNames(Isolate::GetCurrent()->GetCurrentContext()).ToLocalChecked();
  for (uint32_t i = 0; i < keys->Length(); i++)
  {
    Local<Value> key = keys->Get(i);
    this->setAttributStringV8(key, headers->Get(key));
  }
  printf("<WebxHttpTransaction %d>\n", ++leakcount);
}
WebxHttpTransaction::~WebxHttpTransaction()
{
  printf("<WebxHttpTransaction %d>\n", --leakcount);
}
webx::IStream *WebxHttpTransaction::getResponse()
{
  return &this->response;
}
bool WebxHttpTransaction::connect(webx::IStream *stream)
{
  this->opposite = stream;
  return true;
}

void WebxHttpTransaction::free()
{
  _ASSERT(!this->response.output.flush());
  delete this;
}

// ------------------------------------------
// Transaction response

WebxHttpResponse::WebxHttpResponse(WebxHttpTransaction *transaction)
  : output(this, this->completeSync)
{
  this->transaction = transaction;
  this->state = Pending;
}

void WebxHttpResponse::retain()
{
  this->transaction->retain();
}

void WebxHttpResponse::release()
{
  this->transaction->release();
}

bool WebxHttpResponse::connect(IStream *stream)
{
  printf("WebxHttpResponse connect not supported\n");
  return false;
}
bool WebxHttpResponse::write(webx::IData *data)
{
  this->output.push_idle(data);
  return true;
}

void WebxHttpResponse::close()
{
  this->output.complete();
}

struct ResponseHeaders : public webx::StringAttributsVisitor<webx::IAttributsVisitor>
{
  int statusCode;
  v8::Local<v8::Object> headers;

  ResponseHeaders(WebxHttpResponse *response)
    : statusCode(0), headers(Nan::New<v8::Object>())
  {
    response->visitAttributs(this);
  }
  virtual void visitString(const char *name, const char *value) override
  {
    if (name[0] == ':') {
      if (!stricmp(name, ":status")) {
        this->statusCode = atoi(value);
      }
    }
    else this->headers->Set(Nan::New(name).ToLocalChecked(), Nan::New(value).ToLocalChecked());
  }
};

void WebxHttpResponse::completeSync(uv_async_t *handle)
{
  using namespace v8;
  WebxHttpResponse *_this = (WebxHttpResponse *)handle->data;
  Isolate *isolate = Isolate::GetCurrent();
  HandleScope scope(isolate);

  // Initiate the response stream
  if (_this->state == Pending) {
    ResponseHeaders response(_this);
    Local<Value> argv[] = { /*status*/ Nan::New(response.statusCode), /*headers*/ response.headers };
    Local<Function> onBegin = Local<Function>::New(isolate, _this->transaction->onBegin);
    onBegin->Call(isolate->GetCurrentContext()->Global(), 2, argv);
    _this->state = Writing;
  }

  // Write into the response stream
  if (_this->state == Writing) {

    // Write buffers
    if (webx::Ref<webx::IData> output = _this->output.flush())
    {
      Local<Function> onWrite = Local<Function>::New(isolate, _this->transaction->onWrite);
      for (webx::IData* data = output; data; data = data->next.cast<webx::IData>())
      {
        v8::Local<v8::Value> buffer = node::Buffer::Copy(v8::Isolate::GetCurrent(), data->bytes, data->size).ToLocalChecked();
        Local<Value> argv[] = { /*status*/ buffer };
        onWrite->Call(isolate->GetCurrentContext()->Global(), 1, argv);
      }
    }

    // Close the response stream
    if (_this->output.is_completed()) {
      Local<Function> onEnd = Local<Function>::New(isolate, _this->transaction->onEnd);
      onEnd->Call(isolate->GetCurrentContext()->Global(), 0, 0);
      _this->state = Completed;
    }
  }
}
