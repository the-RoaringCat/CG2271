#include <raylib.h>
#include <raymath.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdatomic.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <pthread.h>
#define COMM_IMPLEMENTATION
#include "common.h"


#if defined(PLATFORM_WEB)
    #include <emscripten/emscripten.h>      // Emscripten library
#endif

#define SCREENWIDTH 800
#define SCREENHEIGHT 450

#define DAY 0
#define NIGHT 1

#define MINV2 (Vector2){0,0}
#define MAXV2 (Vector2){SCREENWIDTH, SCREENHEIGHT}

int screenWidth = 800;
int screenHeight = 450;


void UpdateDrawFrame(void);     // Update and Draw one frame
void GetRecCorner(Rectangle rec, Vector2 origin, int rotation, Vector2 corners[4]);
bool CheckCollisionRotatedRec(Rectangle rec_a, Vector2 origin_a, int rotation_a, 
                                Rectangle rec_b, Vector2 origin_b, int rotation_b);

typedef struct BOX {
    Rectangle rec;
    Vector2 centre;
    int rotation;
    Color color;
} BOX;

void *input_thread(void *arg);
int server_fd;
atomic_char key_w;
atomic_char key_a;
atomic_char key_s;
atomic_char key_d;
atomic_char key_night;
atomic_char key_horn;
atomic_char key_start;

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <url> <port>\n", argv[0]);
        return 1;
    }


    // Initialization
    atomic_store(&key_w, 0);
    atomic_store(&key_a, 0);
    atomic_store(&key_s, 0);
    atomic_store(&key_d, 0);
    atomic_store(&key_night, 0);

    InitWindow(SCREENWIDTH, SCREENHEIGHT, "wheelchair");

    //connection
    struct addrinfo hints = {0};
    struct addrinfo *res; //a linked lsit

    hints.ai_family = AF_UNSPEC;    //dont care IPv4 or 6
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;    

    if (getaddrinfo(argv[1], argv[2], &hints, &res) != 0) {
        printf("getaddrinfo fails\n");
        exit(0);
    }

    server_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (server_fd == -1) {
        printf("socket fails.\n");
        exit(0);
    }

    if (connect(server_fd, res->ai_addr, res->ai_addrlen) == -1) {
        printf("connect fails\n");
        exit(0);
    }
    
    int flag = 1;
    setsockopt(server_fd, IPPROTO_TCP, TCP_NODELAY,
           &flag, sizeof(flag));

    pthread_t thread;
    pthread_create(&thread, NULL, input_thread, NULL);

#if defined(PLATFORM_WEB)
    emscripten_set_main_loop(UpdateDrawFrame, 60, 1);
#else
    SetTargetFPS(60);       // Set our game to run at 60 frames-per-second
    //--------------------------------------------------------------------------------------

    // Main game loop
    while (!WindowShouldClose())    // Detect window close button or ESC key
    {
        UpdateDrawFrame();
    }
#endif

    CloseWindow();        // Close window and OpenGL context
    
    return 0;
}



void UpdateDrawFrame(void)
{   
    //init
    static Vector2 direction = {1.0, 0};
    static BOX box = {
        .rec = (Rectangle){(float)SCREENWIDTH / 2.0f,(float)SCREENHEIGHT / 2.0f, 25, 20},
        .centre = (Vector2){10, 10},
        .rotation = 0,
        .color = RED};

    static BOX head = {
        .rec = (Rectangle){(float)SCREENWIDTH / 2.0f ,(float)SCREENHEIGHT / 2.0f, 5, 20},
        .centre = (Vector2){-10, 10},
        .rotation = 0,
        .color = BLACK};        
    static float speed = 1;


    static Rectangle obstacle = {350.0f, 350.0f, 20.0f, 20.0f};

    static int horn_timer = 0;

    //read input
    char is_night;
    float dx = 0;
    float dy = 0;
    int old_rotation = box.rotation;

    if (key_w) {
        box.rec.x += direction.x * speed;
        box.rec.y += direction.y * speed;

        head.rec.x += direction.x * speed;
        head.rec.y += direction.y * speed;

        dx += direction.x * speed;
        dy += direction.y * speed;
    }

    if (key_a) {
        box.rotation = (box.rotation - 2) % 360;


    }

    if (key_s) {
        box.rec.x -= direction.x * speed;
        box.rec.y -= direction.y * speed;

        head.rec.x -= direction.x * speed;
        head.rec.y -= direction.y * speed;

        dx -= direction.x * speed;
        dy -= direction.y * speed;
    }

    if (key_d) {
        box.rotation = (box.rotation + 2) % 360;
    }

    is_night = key_night;
    head.color = is_night ? RAYWHITE : BLACK;

    if (key_horn) {
        horn_timer = 60;
    }

    if (key_start) {
        box.rec.x = (float)SCREENWIDTH / 2.0f;
        box.rec.y = (float)SCREENHEIGHT / 2.0f;

        head.rec.x = (float)SCREENWIDTH / 2.0f;
        head.rec.y = (float)SCREENHEIGHT / 2.0f;

        box.rotation = 0;
        head.rotation = 0;
    }
    int is_collision = 0;
    if (CheckCollisionRotatedRec(obstacle, (Vector2){10, 10}, 0, box.rec, box.centre, box.rotation)) {
        box.rec.x -= dx;
        box.rec.y -= dy;

        head.rec.x -= dx;
        head.rec.y -= dy;

        box.rotation = old_rotation;

        is_collision = 1;
    }
    
    direction = Vector2Rotate((Vector2){1.0, 0}, (float)box.rotation * DEG2RAD); //in radian
    

    // Draw
    BeginDrawing();
        ClearBackground(is_night ? BLACK : RAYWHITE);
        //note rec's x and y are position of pivot, the origin is offset of this pivot from 
        //the topleft corner 
        DrawRectanglePro(box.rec, box.centre, (float)box.rotation, box.color); 
        DrawRectanglePro(head.rec, head.centre, (float)box.rotation, head.color); 
        DrawRectanglePro(obstacle, (Vector2){10, 10}, 0, RED);
        
        if (is_collision) {
            DrawRectanglePro(obstacle, (Vector2){10, 10}, 0, RED);
	        uint8_t data[MAX_DATA_SIZE - 1];
	        data[0] = 1;
	        Send(data, URL_GAME_OUTPUT, server_fd);
        } else {
            DrawRectanglePro(obstacle, (Vector2){10, 10}, 0, GREEN);
        }

        if (horn_timer > 0) {          
            int text_x = (GetScreenWidth() - MeasureText("HORNNNN", 30)) / 2;
            int text_y = (GetScreenHeight() - 30) / 2;

            DrawText("HORNNNN", text_x, text_y, 30 + horn_timer / 2, RED);
            horn_timer--;
        }
    EndDrawing();    
}

void *input_thread(void *arg) {
    while (1) {
        uint8_t data[MAX_DATA_SIZE - 1] = {0};
        uint8_t status_url = 0;
        Recv(data, &status_url, server_fd);
        atomic_store(&key_w, data[0]);
        atomic_store(&key_a, data[1]);
        atomic_store(&key_s, data[2]);
        atomic_store(&key_d, data[3]);
        atomic_store(&key_night, data[4]);
        atomic_store(&key_horn, data[5]);
        atomic_store(&key_start, data[6]);
    }
}

void GetRecCorner(Rectangle rec, Vector2 origin, int rotation, Vector2 corners[4]) {
    //rec store the position of the pivot while origin store the offset of topleft from that
    //pivot

    //four corners relative to the rotation pivot before rotation   
    corners[0] = (Vector2){-origin.x, -origin.y};  //top_left                              
    corners[1] = (Vector2){corners[0].x + rec.width, corners[0].y}; //top_right
    corners[2] = (Vector2){corners[1].x, corners[1].y + rec.height}; //buttom_right
    corners[3] = (Vector2){corners[0].x, corners[0].y + rec.height}; //buttom_left
    
    for (int i = 0; i < 4; i++) {
        //rotate the corner vector (convert to radian)
        corners[i] = Vector2Rotate(corners[i], (float)rotation * DEG2RAD);

        //add back the offset so corner is relative to the canvas
        corners[i].x += rec.x;
        corners[i].y += rec.y;
    }
}

//not a good way since 2 rec can overlap when no corners are in the other rec
//but for moving object, this is good enough
//continuous movement imply the corner must be within the rec just upon collision
//although the rec move discrete, but distance very small, so can agar to continuous
bool CheckCollisionRotatedRec(Rectangle rec_a, Vector2 origin_a, int rotation_a, Rectangle rec_b, Vector2 origin_b, int rotation_b) {
                                    
    Vector2 corners_a[4];
    Vector2 corners_b[4];
    //extract the corners
    GetRecCorner(rec_a, origin_a, rotation_a, corners_a);
    GetRecCorner(rec_b, origin_b, rotation_b, corners_b);

    //first test each point from a against rec b
    //then test each point from b against rec a

    for (int i = 0; i < 4; i++) {
        if (CheckCollisionPointPoly(corners_a[i], corners_b, 4) || CheckCollisionPointPoly(corners_b[i], corners_a, 4)) {
            return true;
        }
    }
    return false;
}
