#include <DiretXMath>

namespace primitives
{
    struct vertex {
        XMFLOAT3 ndcPos;
        XMFLOAT3 colour;
    };
    vertex vertices[] =
    {//Front red, back green
        {XMFLOAT3(-1.0f, 1.0f,-1.0f),XMFLOAT3(1.0f,0.0f,0.0f)}, //Top Left 0 
        {XMFLOAT3(1.0f, 1.0f,-1.0f),XMFLOAT3(1.0f,0.0f,0.0f)}, //Top right 1
        { XMFLOAT3(-1.0f,-1.0f,-1.0f),XMFLOAT3(1.0f,0.0f,0.0f)}, //Bottom Left 2
        {XMFLOAT3(1.0f, -1.0f,-1.0f),XMFLOAT3(1.0f,0.0f,0.0f) }, //Bottom Right 3
        {XMFLOAT3(-1.0f, 1.0f,1.0f),XMFLOAT3(0.0f,1.0f,0.0f)}, //Back Left 4 
        {XMFLOAT3(1.0f, 1.0f,1.0f),XMFLOAT3(0.0f,1.0f,0.0f)}, //Back right 5
        { XMFLOAT3(-1.0f,-1.0f,1.0f),XMFLOAT3(0.0f,1.0f,0.0f)}, //Back Left 6
        {XMFLOAT3(1.0f, -1.0f,1.0f),XMFLOAT3(0.0f,1.0f,0.0f) }, //Back Right 7

            uint16_t indices[] = { 0,1,2,1,3,2, //Front face
                           1,7,3,1,5,7, //Right face 
                           0,2,6,4,0,6,  //Left face
                           4,6,5,5,6,7,  //Back face
                           5,1,0,4,5,0,  //Top face
                           7,2,3,7,6,2,  //Bottom face
                         };
    };

}