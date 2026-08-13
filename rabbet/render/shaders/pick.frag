layout(location = 0) out int oEntityId;
RB_PER_DRAW_BEGIN
RB_PER_DRAW_AT(int uEntityId, 64)
RB_PER_DRAW_END
void main() {
    oEntityId = uEntityId;
}
