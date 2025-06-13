#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#include <string.h>
#include <signal.h>
#include <assert.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/http.h>
#include <event2/keyvalq_struct.h>
#include <event2/buffer.h>
#include "dbi.h"

#ifndef DEBUG
#define DEBUG 1
#endif

#define IGNORE(val) (void)val

#ifndef TEN_K
#define TEN_K 10000
#endif

#ifndef RINGBUFFER_SIZE
// #define RINGBUFFER_SIZE 10
#define RINGBUFFER_SIZE 150
#endif

#ifndef OUTPUT_LINE_SIZE
#define OUTPUT_LINE_SIZE 200
#endif

struct String {
    size_t length; // Length of string (excludes null byte)
    char *buffer; // Buffer *does* include null byte
};

struct RingBuffer {
    size_t offset;
    char **buffers;
};

// All global variables are defined here
struct String global_index_html;
struct String global_script_js;
struct String global_style_css;

DbiProgram global_prog;
DbiRuntime global_dbi;
struct RingBuffer global_ringbuffer;
FILE *global_command_log;

static void ringbuffer_printf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(global_ringbuffer.buffers[global_ringbuffer.offset], OUTPUT_LINE_SIZE, fmt, args);
    va_end(args);

    // printf("%d\n", global_ringbuffer.offset);
    global_ringbuffer.offset++;
    if (global_ringbuffer.offset >= RINGBUFFER_SIZE - 1) {
        global_ringbuffer.offset = 0;
    }
}

enum DbiStatus print_ffi(DbiRuntime dbi)
{
    int argc = dbi_get_argc(dbi);
    struct DbiObject **argv = dbi_get_argv(dbi);
    for (int i = argc - 1; i >= 0; i--) {
        if (argv[i]->type == DBI_INT) {
            ringbuffer_printf("%d", argv[i]->bint);
        } else {
            // printf("%s", argv[i]->bstr);
            ringbuffer_printf("%s", argv[i]->bstr);
        }
    }
    ringbuffer_printf("\n");
    return DBI_STATUS_GOOD;
}

static void signal_cb(evutil_socket_t fd, short event, void *arg)
{
    IGNORE(event);
    fprintf(stderr, "%s signal received\n", strsignal(fd));
    event_base_loopbreak(arg);
}

// Most libevent http functions return 0 on success
// Can use this for setup functions that we expect to succeed before continuing
#define SETUP_SUCCESS(ev_setup_call) assert(ev_setup_call == 0)

// Reduce boilerplate

static void route_log(const char *fmt, ...)
{
    fprintf(stderr, "ROUTE: ");
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
}

void route_404_notfound(struct evhttp_request *req, void *ctx)
{
    route_log("ROUTE: 404 - not found");
    IGNORE(ctx);
    struct evbuffer *reply = evbuffer_new();
    struct evkeyvalq *headers = evhttp_request_get_output_headers(req);
    evhttp_add_header(headers, "Content-Type", "text/html; charset=utf-8");
    evbuffer_add_printf(reply, "<h1>404 - Not Found</h1>");
    evhttp_send_reply(req, HTTP_NOTFOUND, NULL, reply);
    evbuffer_free(reply);
}

void route_index(struct evhttp_request *req, void *ctx)
{
    enum evhttp_cmd_type req_type = evhttp_request_get_command(req);
    if (req_type != EVHTTP_REQ_GET) {
        route_log("ROUTE: index - bad request type");
        route_404_notfound(req, ctx);
        return;
    }
    struct evbuffer *reply = evbuffer_new();
    struct evkeyvalq *headers = evhttp_request_get_output_headers(req);
    evhttp_add_header(headers, "Content-Type", "text/html; charset=utf-8");
    evbuffer_add_printf(reply, global_index_html.buffer);
    evhttp_send_reply(req, HTTP_OK, NULL, reply);
    evbuffer_free(reply);
}

void route_static(struct evhttp_request *req, void *ctx, char *mime_type, struct String *string)
{
    enum evhttp_cmd_type req_type = evhttp_request_get_command(req);
    if (req_type != EVHTTP_REQ_GET) {
        route_log("ROUTE: index - bad request type");
        route_404_notfound(req, ctx);
        return;
    }

    struct evbuffer *reply = evbuffer_new();
    evbuffer_expand(reply, string->length);
    evbuffer_add(reply, string->buffer, string->length);

    char lenbuff[128];
    snprintf(lenbuff, sizeof(lenbuff), "%lu", string->length);
    struct evkeyvalq *headers = evhttp_request_get_output_headers(req);
    evhttp_add_header(headers, "Content-Type", mime_type);
    evhttp_add_header(headers, "Content-Length", lenbuff);
    evhttp_send_reply(req, HTTP_OK, "OK", reply);
    evbuffer_free(reply);
}

void route_css(struct evhttp_request *req, void *ctx)
{
    route_static(req, ctx, "text/css; charset=utf-8", &global_style_css);
}

void route_js(struct evhttp_request *req, void *ctx)
{
    route_static(req, ctx, "text/javascript; charset=utf-8", &global_script_js);
}

void route_get_free(struct evbuffer *buff, struct evhttp_uri *uri, struct evkeyvalq *params)
{
    if (buff) {
        free(buff);
    }
    if (uri) {
        evhttp_uri_free(uri);
    }
    if (params) {
        evhttp_clear_headers(params);
    }
}

void route_get_code(struct evhttp_request *req, void *ctx)
{
    IGNORE(ctx);
    struct evkeyvalq *headers = evhttp_request_get_output_headers(req);
    evhttp_add_header(headers, "Content-Type", "text/plain; charset=utf-8");

    struct evbuffer *reply = evbuffer_new();
    const char *uri = evhttp_request_get_uri(req);
    struct evhttp_uri *decoded = evhttp_uri_parse(uri);
    const char *query = evhttp_uri_get_query(decoded);

    struct evkeyvalq params;
    evhttp_parse_query_str(query, &params);
    const char *lineno_str = evhttp_find_header(&params, "lineno");

    if (!lineno_str) {
        route_log("GET failure: could not find header parameter 'lineno' in %s", query);
        evbuffer_add_printf(reply, "http request requires query parameter 'lineno'");
        evhttp_send_reply(req, HTTP_BADREQUEST, NULL, reply);
    } else {
        int lineno = atoi(lineno_str);
        if (lineno < 1 || lineno >= TEN_K) {
            const char *err = "GET failure: lineno '%s' is invalid";
            route_log(err, lineno_str);
            evbuffer_add_printf(reply, err, lineno_str);
            evhttp_send_reply(req, HTTP_BADREQUEST, NULL, reply);
        } else {
            // Shouldn't log this since it happens constantly
            // route_log("GET: lineno: %d", lineno);
            evbuffer_expand(reply, 150 * DBI_MAX_LINE_LENGTH);
            for (int i = lineno; i < TEN_K; i++) {
                char *line = dbi_get_line(global_prog, i);
                evbuffer_add_printf(reply, "%s\n", line);
                if (i > lineno + 100) break;
            }
            evhttp_send_reply(req, HTTP_OK, NULL, reply);
        }
    }
    route_get_free(reply, decoded, &params);
}

#define MAX_FORM_QUERY_STRING 1024
char post_body[MAX_FORM_QUERY_STRING];

void route_post_code_free(struct evkeyvalq *params, struct evbuffer *reply)
{
    if (params) {
        evhttp_clear_headers(params);
        free(params);
    }
    if (reply) {
        evbuffer_free(reply);
    }
}

static struct evkeyvalq *setup_post(struct evhttp_request *req)
{
    // Get body
    struct evbuffer *buf = evhttp_request_get_input_buffer(req);
    memset(post_body, 0, MAX_FORM_QUERY_STRING-1);
    ev_ssize_t readlen = evbuffer_copyout(buf, post_body, MAX_FORM_QUERY_STRING-1);
    route_log("POST: readlen %d", (int) readlen);
    if (readlen == -1 || readlen == MAX_FORM_QUERY_STRING-1) {
        route_log("POST failure: could not read query");
        return NULL;
    }
    route_log("post_body: %s", post_body);

    // Read parameters from post_body
    struct evkeyvalq *params = malloc(sizeof(*params));
    if (evhttp_parse_query_str(post_body, params) == -1) {
        route_log("POST failure: could not parse query parameters");
        free(params);
        return NULL;
    }
    return params;
}

void get_ip_addr(char **ipaddr, ev_uint16_t *port, struct evhttp_request *req)
{
    struct evhttp_connection *conn = evhttp_request_get_connection(req);
    if (!conn) {
        route_log("POST failure: Could not get connection");
        return;
    }
    evhttp_connection_get_peer(conn, ipaddr, port);
}

void route_post_code(struct evhttp_request *req, void *ctx)
{
    struct evbuffer *reply = evbuffer_new();
    struct evkeyvalq *params = setup_post(req);
    if (!params) {
        route_404_notfound(req, ctx);
        goto exit;
    }
    const char *line = evhttp_find_header(params, "line");
    struct evkeyvalq *headers = evhttp_request_get_output_headers(req);
    evhttp_add_header(headers, "Content-Type", "text/plain; charset=utf-8");
    if (!line) {
        route_log("POST failure: could not find header parameter 'line'");
        evbuffer_add_printf(reply, "http request required query parameter 'line'");
        evhttp_send_reply(req, HTTP_BADREQUEST, NULL, reply);
    } else {
        route_log("POST: line: %s", line);
        bool ret = dbi_compile_string(global_prog, (char *)line);
        if (!ret) {
            printf("%s", dbi_strerror());
            evbuffer_add_printf(reply, "%s", dbi_strerror());
        } else {
            char *ipaddr = "";
            ev_uint16_t port = 0;
            get_ip_addr(&ipaddr, &port, req);
            fprintf(global_command_log, "%s : rem IP: %s:%u\n", line, ipaddr, port);
            evbuffer_add_printf(reply, "all gucci");
        }
        evhttp_send_reply(req, HTTP_OK, NULL, reply);
    }
exit:
    route_post_code_free(params, reply);
}

void route_stdout(struct evhttp_request *req, void *ctx)
{
    enum evhttp_cmd_type req_type = evhttp_request_get_command(req);
    if (req_type != EVHTTP_REQ_GET) {
        route_log("ROUTE: stdout - bad request type");
        route_404_notfound(req, ctx);
        return;
    }

    struct evkeyvalq *headers = evhttp_request_get_output_headers(req);
    evhttp_add_header(headers, "Content-Type", "text/plain; charset=utf-8");

    struct evbuffer *reply = evbuffer_new();
    evbuffer_expand(reply, RINGBUFFER_SIZE * OUTPUT_LINE_SIZE);

    for (int i = (int)global_ringbuffer.offset; i >= 0; i--) {
        evbuffer_add_printf(reply, "%s", global_ringbuffer.buffers[i]);
    }
    for (int i = RINGBUFFER_SIZE - 1; i > ((int)global_ringbuffer.offset); i--) {
        evbuffer_add_printf(reply, "%s", global_ringbuffer.buffers[i]);
    }
    evhttp_send_reply(req, HTTP_OK, NULL, reply);
    evbuffer_free(reply);
}

void route_run(struct evhttp_request *req, void *ctx)
{
    enum evhttp_cmd_type req_type = evhttp_request_get_command(req);
    if (req_type != EVHTTP_REQ_POST) {
        route_log("ROUTE: run - bad request type");
        route_404_notfound(req, ctx);
        return;
    }

    struct evkeyvalq *headers = evhttp_request_get_output_headers(req);
    evhttp_add_header(headers, "Content-Type", "text/plain; charset=utf-8");

    struct evbuffer *reply = evbuffer_new();

    printf("running program\n");
    global_dbi = dbi_runtime_new();
    enum DbiStatus ret = dbi_run(global_dbi, global_prog);
    if (ret == DBI_STATUS_ERROR) {
        ringbuffer_printf("%s", dbi_strerror());
        evbuffer_add_printf(reply, "%s", dbi_strerror());
    } else {
        evbuffer_add_printf(reply, "%s", "all good");
    }
    dbi_runtime_free(global_dbi);

    evhttp_send_reply(req, HTTP_OK, NULL, reply);
    evbuffer_free(reply);
    printf("done\n");
}

void route_code(struct evhttp_request *req, void *ctx)
{
    enum evhttp_cmd_type req_type = evhttp_request_get_command(req);
    if (req_type == EVHTTP_REQ_GET) {
        route_get_code(req, ctx);
    } else if (req_type == EVHTTP_REQ_POST) {
        route_post_code(req, ctx);
    } else {
        route_log("ROUTE: code - bad request type");
        route_404_notfound(req, ctx);
        return;
    }
}

void load_static_file(struct String *string, char *filename)
{
    FILE *file = fopen(filename, "r");

    // Get file size
    fseek(file, 0L, SEEK_END);
    long len = ftell(file);
    assert(len > 0);
    rewind(file);

    // Read file
    char *buff = calloc(len + 1, 1);
    long fread_len = fread(buff, 1, len, file);
    assert(fread_len == len);
    fclose(file);

    string->length = (size_t)len;
    string->buffer = buff;
}

void free_static_file(struct String *string)
{
    free(string->buffer);
}

void ringbuffer_init(struct RingBuffer *ringbuffer)
{
    ringbuffer->offset = 0;
    ringbuffer->buffers = calloc(RINGBUFFER_SIZE, sizeof(*ringbuffer->buffers));
    for (int i = 0; i < RINGBUFFER_SIZE; i++) {
        ringbuffer->buffers[i] = calloc(OUTPUT_LINE_SIZE, 1);
    }
}

void ringbuffer_free(struct RingBuffer *ringbuffer)
{
    for (int i = 0; i < RINGBUFFER_SIZE; i++) {
        free(ringbuffer->buffers[i]);
    }
    free(ringbuffer->buffers);
}

int main(void)
{ 
    setlocale(LC_ALL, "en_US.UTF-8");

    // ***************************************************
    // ****************** get HTML file ******************
    // ***************************************************
    load_static_file(&global_index_html, "index.html");
    load_static_file(&global_script_js,  "script.js");
    load_static_file(&global_style_css,  "style.css");

    // ***************************************************
    // ******************* BASIC setup *******************
    // ***************************************************
    global_prog = dbi_program_new();
    dbi_register_command(global_prog, "PRINT", print_ffi, -1);
#if DEBUG
    bool ret = dbi_compile_file(global_prog, "init.bas");
#else
    bool ret = dbi_compile_file(global_prog, "code.bas");
#endif

    if (!ret) {
        printf("%s\n", dbi_strerror());
    }
    assert(ret);
    ringbuffer_init(&global_ringbuffer);

    // Command log
    global_command_log = fopen("command_log.bas", "a");
    assert(global_command_log);

    // ***************************************************
    // ***************** libevent setup ******************
    // ***************************************************
#if DEBUG
    ev_uint16_t http_port = 8080;
#else 
    ev_uint16_t http_port = 80;
#endif
    char *http_addr = "0.0.0.0";
    struct event_base *base;
    struct evhttp *http_server;
    struct event *sig_int;

    base = event_base_new();

    // Setup
    http_server = evhttp_new(base);
    evhttp_set_allowed_methods(http_server, EVHTTP_REQ_GET | EVHTTP_REQ_POST);
    SETUP_SUCCESS(evhttp_bind_socket(http_server, http_addr, http_port));

    // Top level routes
    evhttp_set_cb(http_server, "/",          route_index,  NULL);
    evhttp_set_cb(http_server, "/code",      route_code,   NULL);
    evhttp_set_cb(http_server, "/run",       route_run,    NULL);
    evhttp_set_cb(http_server, "/stdout",    route_stdout, NULL);
    evhttp_set_cb(http_server, "/script.js", route_js,     NULL);
    evhttp_set_cb(http_server, "/style.css", route_css,    NULL);
    // evhttp_set_cb(http_server, "/post", route_new_survey, NULL);
    // evhttp_set_gencb(http_server, send_file_to_user, NULL);

    sig_int = evsignal_new(base, SIGINT, signal_cb, base);
    event_add(sig_int, NULL);

    printf("Listening requests on http://%s:%d\n", http_addr, http_port);
    event_base_dispatch(base);

    
    // ***************************************************
    // ******************* Save output *******************
    // ***************************************************
    FILE *sav = fopen("code.bas", "w+");
    for (int i = 1; i < TEN_K; i++) {
        char *line = dbi_get_line(global_prog, i);
        fprintf(sav, "%s\n", line);
    }
    fclose(sav);

    // Close logger
    fclose(global_command_log);

    // ***************************************************
    // ********************* Cleanup *********************
    // ***************************************************
    free_static_file(&global_index_html);
    free_static_file(&global_script_js);
    free_static_file(&global_style_css);

    // BASIC frees
    dbi_program_free(global_prog);
    ringbuffer_free(&global_ringbuffer);

    // libevent frees
    evhttp_free(http_server);
    event_free(sig_int);
    event_base_free(base);
}

