#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

static void in_thu_muc_hien_tai(void)
{
    size_t kich_thuoc = 128;
    for (;;) {
        char *duong_dan = malloc(kich_thuoc);
        if (duong_dan == NULL) { perror("malloc"); return; }
        if (getcwd(duong_dan, kich_thuoc) != NULL) {
            puts(duong_dan);
            free(duong_dan);
            return;
        }
        int loi = errno;
        free(duong_dan);
        /* ERANGE nghia la buffer qua nho; thu lai voi buffer lon gap doi. */
        if (loi != ERANGE) { errno = loi; perror("pwd"); return; }
        if (kich_thuoc > (size_t)-1 / 2) {
            fprintf(stderr, "pwd: duong dan qua dai\n");
            return;
        }
        kich_thuoc *= 2;
    }
}

static void in_noi_dung_file(const char *duong_dan)
{
    /* fopen tu xu ly ca duong dan tuong doi (theo cwd) va tuyet doi. */
    FILE *file = fopen(duong_dan, "r");
    if (file == NULL) { perror(duong_dan); return; }
    /* fgetc tra ve int de phan biet byte hop le voi EOF. */
    int c;
    while ((c = fgetc(file)) != EOF) {
        if (putchar(c) == EOF) { perror("cat: stdout"); break; }
    }
    if (ferror(file)) perror(duong_dan);
    if (fclose(file) == EOF) perror("fclose");
}

int main(void)
{
    char *dong = NULL;
    size_t suc_chua = 0;
    for (;;) {
        printf("tinyshell> ");
        fflush(stdout); 
        if (getline(&dong, &suc_chua, stdin) == -1) break;
        dong[strcspn(dong, "\r\n")] = '\0';
        char *lenh = dong;
        while (isspace((unsigned char)*lenh)) ++lenh;
        if (*lenh == '\0') continue;
        char *tham_so = lenh;
        while (*tham_so && !isspace((unsigned char)*tham_so)) ++tham_so;
        if (*tham_so) *tham_so++ = '\0';
        while (isspace((unsigned char)*tham_so)) ++tham_so;
        char *cuoi = tham_so + strlen(tham_so);
        while (cuoi > tham_so && isspace((unsigned char)cuoi[-1])) --cuoi;
        *cuoi = '\0';

        if (strcmp(lenh, "exit") == 0) {
            if (*tham_so) fprintf(stderr, "Cu phap: exit\n");
            else break;
        } else if (strcmp(lenh, "pwd") == 0) {
            if (*tham_so) fprintf(stderr, "Cu phap: pwd\n");
            else in_thu_muc_hien_tai();
        } else if (strcmp(lenh, "cd") == 0) {
            if (!*tham_so) fprintf(stderr, "Cu phap: cd <directory>\n");
            else if (chdir(tham_so) == -1) perror(tham_so);
        } else if (strcmp(lenh, "cat") == 0) {
            if (!*tham_so) fprintf(stderr, "Cu phap: cat <file>\n");
            else in_noi_dung_file(tham_so);
        } else {
            fprintf(stderr, "Lenh khong ho tro: %s (cd, cat, pwd, exit)\n", lenh);
        }
    }
    int loi_doc = ferror(stdin);
    if (loi_doc) perror("getline");
    free(dong);
    return loi_doc ? EXIT_FAILURE : EXIT_SUCCESS;
}
