/*
* Student Management REST API Server - College Version
* Features: Admin (college) and Student auth, registration, profile management,
*           admin-managed academics (CGPA, attendance), per-student privacy
* Compile: gcc -o bank_server bank_server.c -lws2_32
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <time.h>

#pragma comment(lib, "ws2_32.lib")

#define PORT 8080
#define BUFFER_SIZE 8192
#define ADMIN_PASSWORD "admin123"
#define DEFAULT_STUDENT_PASSWORD "student123"

typedef struct Student {
    int studentId;
    char name[100];
    char password[100];
    char email[120];
    char department[80];
    int year;
    double cgpa;
    double attendance; // percentage 0-100
    struct Student* next;
} Student;

typedef struct {
    Student* head;
    int nextStudentId;
} StudentData;

StudentData g_students = {0};

void initStudentData();
void saveToFile();
void loadFromFile();
Student* findStudent(int studentId);
void handleRequest(SOCKET client_socket, char* request);
void sendResponse(SOCKET client_socket, int status, const char* body);
void sendCORSHeaders(SOCKET client_socket);
void parseJSON(char* json, char* key, char* value);
double parseJSONNumber(char* json, char* key);
void getCurrentTimestamp(char* buffer);

int main() {
    WSADATA wsa;
    SOCKET server_socket, client_socket;
    struct sockaddr_in server, client;
    int c, recv_size;
    char buffer[BUFFER_SIZE];

    printf("===========================================\n");
    printf("  Student Management REST API Server\n");
    printf("===========================================\n");
    printf("Admin Password: %s\n", ADMIN_PASSWORD);

    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        printf("WSAStartup failed: %d\n", WSAGetLastError());
        return 1;
    }

    initStudentData();
    loadFromFile();

    if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET) {
        printf("Socket creation failed: %d\n", WSAGetLastError());
        return 1;
    }

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(PORT);

    if (bind(server_socket, (struct sockaddr *)&server, sizeof(server)) == SOCKET_ERROR) {
        printf("Bind failed: %d\n", WSAGetLastError());
        return 1;
    }

    listen(server_socket, 3);
    printf("Server running on: http://localhost:%d\n", PORT);
    printf("===========================================\n\n");

    c = sizeof(struct sockaddr_in);
    while ((client_socket = accept(server_socket, (struct sockaddr *)&client, &c)) != INVALID_SOCKET) {
        recv_size = recv(client_socket, buffer, BUFFER_SIZE, 0);
        if (recv_size > 0) {
            buffer[recv_size] = '\0';
            handleRequest(client_socket, buffer);
        }
        closesocket(client_socket);
    }

    closesocket(server_socket);
    WSACleanup();
    return 0;
}

void initStudentData() {
    g_students.head = NULL;
    g_students.nextStudentId = 1001;
}

void getCurrentTimestamp(char* buffer) {
    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    strftime(buffer, 50, "%Y-%m-%d %H:%M:%S", t);
}

// no transactions in student system

void handleRequest(SOCKET client_socket, char* request) {
    char method[10], path[256];
    sscanf(request, "%s %s", method, path);

    printf("[%s] %s\n", method, path);

    if (strcmp(method, "OPTIONS") == 0) {
        sendCORSHeaders(client_socket);
        char response[256];
        sprintf(response, "HTTP/1.1 200 OK\r\n\r\n");
        send(client_socket, response, strlen(response), 0);
        return;
    }

    // POST /api/admin/login
    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/admin/login") == 0) {
        char* body_start = strstr(request, "\r\n\r\n");
        if (body_start) {
            body_start += 4;
            char password[100];
            parseJSON(body_start, "password", password);

            if (strcmp(password, ADMIN_PASSWORD) == 0) {
                sendResponse(client_socket, 200, "{\"success\":true}");
                printf("  ✓ Admin login successful\n");
            } else {
                sendResponse(client_socket, 401, "{\"error\":\"Invalid password\"}");
                printf("  ✗ Admin login failed\n");
            }
        }
        return;
    }

    // POST /api/student/login
    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/student/login") == 0) {
        char* body_start = strstr(request, "\r\n\r\n");
        if (body_start) {
            body_start += 4;
            char password[100];
            int studentId = (int)parseJSONNumber(body_start, "studentId");
            parseJSON(body_start, "password", password);

            Student* stu = findStudent(studentId);
            if (stu == NULL) {
                sendResponse(client_socket, 404, "{\"error\":\"Student not found\"}");
                printf("  ✗ Student #%d not found\n", studentId);
                return;
            }

            if (strcmp(stu->password, password) == 0) {
                char resp[1024];
                sprintf(resp,
                    "{\"success\":true,\"studentId\":%d,\"name\":\"%s\",\"email\":\"%s\",\"department\":\"%s\",\"year\":%d,\"cgpa\":%.2f,\"attendance\":%.2f}",
                    stu->studentId, stu->name, stu->email, stu->department, stu->year, stu->cgpa, stu->attendance);
                sendResponse(client_socket, 200, resp);
                printf("  ✓ Student login: #%d - %s\n", studentId, stu->name);
            } else {
                sendResponse(client_socket, 401, "{\"error\":\"Invalid password\"}");
                printf("  ✗ Invalid password for #%d\n", studentId);
            }
        }
        return;
    }

    // GET /api/students?adminPassword=...
    if (strcmp(method, "GET") == 0 && strncmp(path, "/api/students", 13) == 0 && strchr(path, '?') != NULL) {
        char* pwPos = strstr(path, "adminPassword=");
        if (pwPos) {
            pwPos += strlen("adminPassword=");
            char adminPw[128] = {0};
            int i = 0;
            while (pwPos[i] && pwPos[i] != '&' && i < 120) { adminPw[i] = pwPos[i]; i++; }
            adminPw[i] = '\0';
            if (strcmp(adminPw, ADMIN_PASSWORD) != 0) {
                sendResponse(client_socket, 401, "{\"error\":\"Unauthorized\"}");
                return;
            }

            char body[BUFFER_SIZE] = "[";
            Student* current = g_students.head;
            int first = 1;
            while (current != NULL) {
                char item[512];
                sprintf(item,
                    "%s{\"studentId\":%d,\"name\":\"%s\",\"email\":\"%s\",\"department\":\"%s\",\"year\":%d,\"cgpa\":%.2f,\"attendance\":%.2f}",
                    first ? "" : ",", current->studentId, current->name, current->email, current->department, current->year, current->cgpa, current->attendance);
                strcat(body, item);
                first = 0;
                current = current->next;
            }
            strcat(body, "]");
            sendResponse(client_socket, 200, body);
            printf("  ✓ Retrieved all students (admin)\n");
            return;
        }
    }

    // no transactions endpoints in student system

    // POST /api/students/{id}/me  (student views own details by password)
    if (strcmp(method, "POST") == 0 && strstr(path, "/api/students/") && strstr(path, "/me")) {
        int studentId;
        sscanf(path, "/api/students/%d/me", &studentId);
        char* body_start = strstr(request, "\r\n\r\n");
        if (body_start) {
            body_start += 4;
            char password[100];
            parseJSON(body_start, "password", password);
            Student* stu = findStudent(studentId);
            if (stu == NULL) { sendResponse(client_socket, 404, "{\"error\":\"Student not found\"}"); return; }
            if (strcmp(stu->password, password) != 0) { sendResponse(client_socket, 401, "{\"error\":\"Unauthorized\"}"); return; }
            char resp[1024];
            sprintf(resp, "{\"studentId\":%d,\"name\":\"%s\",\"email\":\"%s\",\"department\":\"%s\",\"year\":%d,\"cgpa\":%.2f,\"attendance\":%.2f}",
                stu->studentId, stu->name, stu->email, stu->department, stu->year, stu->cgpa, stu->attendance);
            sendResponse(client_socket, 200, resp);
            printf("  ✓ Retrieved student #%d (self)\n", studentId);
        }
        return;
    }

    // POST /api/student/register
    if (strcmp(method, "POST") == 0 && strcmp(path, "/api/student/register") == 0) {
        char* body_start = strstr(request, "\r\n\r\n");
        if (body_start) {
            body_start += 4;
            char name[100], password[100], email[120], department[80];
            int year;

            parseJSON(body_start, "name", name);
            parseJSON(body_start, "password", password);
            parseJSON(body_start, "email", email);
            parseJSON(body_start, "department", department);
            year = (int)parseJSONNumber(body_start, "year");

            if (strlen(name) == 0) { sendResponse(client_socket, 400, "{\"error\":\"Name is required\"}"); return; }
            if (strlen(password) < 4) { sendResponse(client_socket, 400, "{\"error\":\"Password must be at least 4 characters\"}"); return; }
            if (year < 1 || year > 6) { sendResponse(client_socket, 400, "{\"error\":\"Year must be between 1 and 6\"}"); return; }

            Student* stu = (Student*)malloc(sizeof(Student));
            stu->studentId = g_students.nextStudentId++;
            strcpy(stu->name, name);
            strcpy(stu->password, password);
            strcpy(stu->email, email);
            strcpy(stu->department, department);
            stu->year = year;
            stu->cgpa = 0.0;
            stu->attendance = 0.0;
            stu->next = g_students.head;
            g_students.head = stu;

            char resp[512];
            sprintf(resp, "{\"studentId\":%d,\"name\":\"%s\"}", stu->studentId, stu->name);
            sendResponse(client_socket, 201, resp);
            saveToFile();
            printf("  ✓ Student registered: #%d - %s\n", stu->studentId, stu->name);
        }
        return;
    }

    // POST /api/students/{id}/update (student updates own profile fields)
    if (strcmp(method, "POST") == 0 && strstr(path, "/api/students/") && strstr(path, "/update")) {
        int studentId;
        sscanf(path, "/api/students/%d/update", &studentId);
        char* body_start = strstr(request, "\r\n\r\n");
        if (body_start) {
            body_start += 4;
            char password[100];
            char name[100] = ""; char email[120] = ""; char department[80] = "";
            int year = 0;
            parseJSON(body_start, "password", password);
            parseJSON(body_start, "name", name);
            parseJSON(body_start, "email", email);
            parseJSON(body_start, "department", department);
            year = (int)parseJSONNumber(body_start, "year");

            Student* stu = findStudent(studentId);
            if (stu == NULL) { sendResponse(client_socket, 404, "{\"error\":\"Student not found\"}"); return; }
            if (strcmp(stu->password, password) != 0) { sendResponse(client_socket, 401, "{\"error\":\"Unauthorized\"}"); return; }

            if (strlen(name) > 0) strcpy(stu->name, name);
            if (strlen(email) > 0) strcpy(stu->email, email);
            if (strlen(department) > 0) strcpy(stu->department, department);
            if (year >= 1 && year <= 6) stu->year = year;

            char resp[1024];
            sprintf(resp, "{\"studentId\":%d,\"name\":\"%s\",\"email\":\"%s\",\"department\":\"%s\",\"year\":%d,\"cgpa\":%.2f,\"attendance\":%.2f}",
                stu->studentId, stu->name, stu->email, stu->department, stu->year, stu->cgpa, stu->attendance);
            sendResponse(client_socket, 200, resp);
            saveToFile();
            printf("  ✓ Student updated (self): #%d\n", studentId);
        }
        return;
    }

    // POST /api/admin/students/{id}/academics (admin sets cgpa, attendance)
    if (strcmp(method, "POST") == 0 && strstr(path, "/api/admin/students/") && strstr(path, "/academics")) {
        int studentId;
        sscanf(path, "/api/admin/students/%d/academics", &studentId);
        char* body_start = strstr(request, "\r\n\r\n");
        if (body_start) {
            body_start += 4;
            char adminPassword[128];
            double cgpa = parseJSONNumber(body_start, "cgpa");
            double attendance = parseJSONNumber(body_start, "attendance");
            parseJSON(body_start, "adminPassword", adminPassword);

            if (strcmp(adminPassword, ADMIN_PASSWORD) != 0) { sendResponse(client_socket, 401, "{\"error\":\"Unauthorized\"}"); return; }
            Student* stu = findStudent(studentId);
            if (stu == NULL) { sendResponse(client_socket, 404, "{\"error\":\"Student not found\"}"); return; }
            if (cgpa < 0.0) cgpa = 0.0; if (cgpa > 10.0) cgpa = 10.0;
            if (attendance < 0.0) attendance = 0.0; if (attendance > 100.0) attendance = 100.0;
            stu->cgpa = cgpa;
            stu->attendance = attendance;
            char resp[512];
            sprintf(resp, "{\"studentId\":%d,\"cgpa\":%.2f,\"attendance\":%.2f}", stu->studentId, stu->cgpa, stu->attendance);
            sendResponse(client_socket, 200, resp);
            saveToFile();
            printf("  ✓ Academics updated (admin): #%d\n", studentId);
        }
        return;
    }

    // DELETE /api/admin/students/{id}?adminPassword=...
    if (strcmp(method, "DELETE") == 0 && strstr(path, "/api/admin/students/")) {
        int studentId;
        sscanf(path, "/api/admin/students/%d", &studentId);
        char* pwPos = strstr(path, "adminPassword=");
        if (!pwPos) { sendResponse(client_socket, 401, "{\"error\":\"Unauthorized\"}"); return; }
        pwPos += strlen("adminPassword=");
        char adminPw[128] = {0}; int i = 0; while (pwPos[i] && pwPos[i] != '&' && i < 120) { adminPw[i] = pwPos[i]; i++; } adminPw[i] = '\0';
        if (strcmp(adminPw, ADMIN_PASSWORD) != 0) { sendResponse(client_socket, 401, "{\"error\":\"Unauthorized\"}"); return; }

        Student* current = g_students.head;
        Student* prev = NULL;
        while (current != NULL && current->studentId != studentId) { prev = current; current = current->next; }
        if (current == NULL) { sendResponse(client_socket, 404, "{\"error\":\"Student not found\"}"); return; }
        if (prev == NULL) { g_students.head = current->next; } else { prev->next = current->next; }
        printf("  ✓ Student deleted: #%d - %s\n", current->studentId, current->name);
        free(current);
        sendResponse(client_socket, 200, "{\"message\":\"Student deleted\"}");
        saveToFile();
        return;
    }

    sendResponse(client_socket, 404, "{\"error\":\"Not found\"}");
}

void sendResponse(SOCKET client_socket, int status, const char* body) {
    char response[BUFFER_SIZE];
    char* status_text = status == 200 ? "OK" : 
                        status == 201 ? "Created" : 
                        status == 400 ? "Bad Request" : 
                        status == 401 ? "Unauthorized" : 
                        "Not Found";

    sprintf(response,
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: application/json\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, DELETE, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Content-Length: %d\r\n"
        "\r\n%s",
        status, status_text, (int)strlen(body), body);

    send(client_socket, response, strlen(response), 0);
}

void sendCORSHeaders(SOCKET client_socket) {
    char response[512];
    sprintf(response,
        "HTTP/1.1 200 OK\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, DELETE, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "\r\n");
    send(client_socket, response, strlen(response), 0);
}

Student* findStudent(int studentId) {
    Student* current = g_students.head;
    while (current != NULL) {
        if (current->studentId == studentId) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

void parseJSON(char* json, char* key, char* value) {
    char search[110];
    sprintf(search, "\"%s\":\"", key);
    char* start = strstr(json, search);
    if (start) {
        start += strlen(search);
        char* end = strchr(start, '"');
        if (end) {
            int len = end - start;
            strncpy(value, start, len);
            value[len] = '\0';
        }
    } else {
        value[0] = '\0';
    }
}

double parseJSONNumber(char* json, char* key) {
    char search[110];
    sprintf(search, "\"%s\":", key);
    char* start = strstr(json, search);
    if (start) {
        start += strlen(search);
        return atof(start);
    }
    return 0.0;
}

void saveToFile() {
    FILE* file = fopen("student_data.txt", "w");
    if (file == NULL) {
        printf("Error: Cannot save to file\n");
        return;
    }

    fprintf(file, "%d\n", g_students.nextStudentId);
    fprintf(file, "%d\n", 0);

    Student* current = g_students.head;
    while (current != NULL) {
        fprintf(file, "STUDENT|%d|%s|%s|%s|%s|%d|%.2f|%.2f\n",
            current->studentId, current->name, current->password, current->email,
            current->department, current->year, current->cgpa, current->attendance);
        current = current->next;
    }

    fclose(file);
}

void loadFromFile() {
    FILE* file = fopen("student_data.txt", "r");
    if (file == NULL) {
        printf("No existing data file found. Starting fresh.\n");
        // Seed a default student
        Student* stu = (Student*)malloc(sizeof(Student));
        stu->studentId = g_students.nextStudentId++;
        strcpy(stu->name, "Default Student");
        strcpy(stu->password, DEFAULT_STUDENT_PASSWORD);
        strcpy(stu->email, "student@example.edu");
        strcpy(stu->department, "CSE");
        stu->year = 1;
        stu->cgpa = 0.0;
        stu->attendance = 0.0;
        stu->next = g_students.head;
        g_students.head = stu;
        saveToFile();
        printf("Seeded default student: id=%d password=%s\n", stu->studentId, DEFAULT_STUDENT_PASSWORD);
        return;
    }

    fscanf(file, "%d\n", &g_students.nextStudentId);
    int unused; fscanf(file, "%d\n", &unused);

    char line[1024];
    int studentCount = 0;

    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "ACCOUNT|", 8) == 0) {
            // migrate legacy bank account entry into student with minimal fields
            int legacyId; char legacyName[100]; char legacyPw[100]; double legacyBalance;
            Student* stu = (Student*)malloc(sizeof(Student));
            sscanf(line, "ACCOUNT|%d|%99[^|]|%99[^|]|%lf\n", &legacyId, legacyName, legacyPw, &legacyBalance);
            stu->studentId = legacyId;
            strcpy(stu->name, legacyName);
            strcpy(stu->password, legacyPw);
            strcpy(stu->email, "migrated@example.edu");
            strcpy(stu->department, "GENERAL");
            stu->year = 1;
            stu->cgpa = 0.0;
            stu->attendance = 0.0;
            stu->next = g_students.head;
            g_students.head = stu;
            studentCount++;
        } else if (strncmp(line, "STUDENT|", 8) == 0) {
            Student* stu = (Student*)malloc(sizeof(Student));
            sscanf(line, "STUDENT|%d|%99[^|]|%99[^|]|%119[^|]|%79[^|]|%d|%lf|%lf\n",
                &stu->studentId, stu->name, stu->password, stu->email, stu->department, &stu->year, &stu->cgpa, &stu->attendance);
            stu->next = g_students.head;
            g_students.head = stu;
            studentCount++;
        }
    }

    fclose(file);
    printf("Loaded %d students from database\n", studentCount);
}