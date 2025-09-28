struct mru_node{
    struct mru_node* next;
    struct mru_node* prev;
    int pid;
    int va;
    void*pa;
};
void * mru_init(void * pa_start, int num_pages, struct mru_node * map[]);

void move_to_end(void * pa);

void move_to_head_and_set(void * pa,int pid,int va);

void* mru_swapout();
void* lru_swapout();

struct mru_node* mru_get_end();

void mru_dump(int n);