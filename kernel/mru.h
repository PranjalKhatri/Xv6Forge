struct mru_node{
    struct mru_node* next;
    struct mru_node* prev;
    int pid;
    int va;
    void*pa;
    int ref_cnt;
};
void * mru_init(void * pa_start, int num_pages, struct mru_node * map[]);
int PA2IDX(void *pa);
void move_to_end(void * pa);

void move_to_head_and_set(void * pa,int pid,int va);

void* mru_swapout();
void* lru_swapout();

void mru_dump(int n);

int mru_incref_helper(uint64 pa);

int mru_decref_helper(uint64 pa);

int get_refcnt(uint64 pa);
