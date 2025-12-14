#ifndef BROWSE_ERROR_PAGES_H
#define BROWSE_ERROR_PAGES_H

// Simple HTML for a 404 Not Found error.
static const char* g_html_404 = 
"HTTP/1.0 404 Not Found\r\nContent-Type: text/html\r\n\r\n"
"<html><head><title>404 Not Found</title></head>"
"<body><h1>404 Not Found</h1>"
"<p>The requested resource could not be found on this server.</p>"
"<hr><p>ChucklesOS Browser</p></body></html>";

// Simple HTML for a 403 Forbidden error.
static const char* g_html_403 = 
"HTTP/1.0 403 Forbidden\r\nContent-Type: text/html\r\n\r\n"
"<html><head><title>403 Forbidden</title></head>"
"<body><h1>403 Forbidden</h1>"
"<p>You do not have permission to access this resource.</p>"
"<hr><p>ChucklesOS Browser</p></body></html>";

#endif