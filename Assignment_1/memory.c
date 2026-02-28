#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void lost_ref(void) {
    int *ptr = (int *)malloc(sizeof(int));
    if (!ptr) return;

    *ptr = 123;
    ptr = NULL;
}

static void pointer_nofree(void) {
    int *ptr = (int *)malloc(10 * sizeof(int));
    if (!ptr) return;

    ptr[0] = 42;

    ptr = (int *)malloc(20 * sizeof(int));
    if (!ptr) return;
    ptr[0] = 84;
}

static void early_return(void) {
    int error_condition = 1;

    char *ptr = (char *)malloc(100);
    if (!ptr) return;

    strcpy(ptr, "allocated then returned early");

    if (error_condition) return;
    free(ptr);
}

static void loop_no_free(void) {
    for (int i = 0; i < 1000; i++) {
        int *temp = (int *)malloc(sizeof(int));
        if (!temp) return;
        *temp = i;
    }
}

static void function_scope_leave(void) {
    void *ptr = malloc(10);
    (void)ptr;
}

struct LLNode {
    int data;
    struct LLNode *next;
    struct LLNode *prev;
};

static void free_list(struct LLNode *head) {
    while (head) {
        struct LLNode *temp = head;
        head = head->next;
        free(temp);
    }
}

static void dont_free_all_data(void) {
    struct LLNode *head = (struct LLNode *)malloc(sizeof(struct LLNode));
    if (!head) return;

    head->data = 0;
    head->prev = NULL;
    head->next = NULL;

    struct LLNode *tail = head;
    for (int i = 1; i < 5; i++) {
        struct LLNode *n = (struct LLNode *)malloc(sizeof(struct LLNode));
        if (!n) break;

        n->data = i;
        n->prev = tail;
        n->next = NULL;
        tail->next = n;
        tail = n;
    }

    if (head->next) {
        free_list(head->next);
        head->next = NULL;
    }
}

static void corruption(void) {
    char buffer[10];
    strcpy(buffer, "This string is definitely longer than 10 bytes!");
    printf("Buffer contents: %s\n", buffer);
}

static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s --LEAK <1..7>\n", prog);
    fprintf(stderr, "  1 lost_ref\n");
    fprintf(stderr, "  2 pointer_nofree\n");
    fprintf(stderr, "  3 early_return\n");
    fprintf(stderr, "  4 loop_no_free\n");
    fprintf(stderr, "  5 function_scope_leave\n");
    fprintf(stderr, "  6 dont_free_all_data (linked list)\n");
    fprintf(stderr, "  7 corruption (buffer overflow)\n");
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "--LEAK") != 0) {
        fprintf(stderr, "Unknown flag: %s\n", argv[1]);
        usage(argv[0]);
        return 1;
    }

    int selec = atoi(argv[2]);

    switch (selec) {
        case 1: lost_ref(); break;
        case 2: pointer_nofree(); break;
        case 3: early_return(); break;
        case 4: loop_no_free(); break;
        case 5: function_scope_leave(); break;
        case 6: dont_free_all_data(); break;
        case 7: corruption(); break;
        default:
            fprintf(stderr, "Invalid test number: %d\n", selec);
            usage(argv[0]);
            return 1;
    }

    return 0;
}