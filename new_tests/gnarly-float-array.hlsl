typedef int ai32[1];
typedef float af32[1];
void inc(inout float x) { x *= -1; }
int main() : OUT
{
    ai32 x = { 42 };
    inc(((af32)x)[0]);
    return x[0];
}
