#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#define SO_SUAT_CHIEU 1000
#define VE_MOI_SUAT 200
#define SO_QUAY 4

int ve_con_lai[SO_SUAT_CHIEU];
int tong_ve_con_lai = SO_SUAT_CHIEU * VE_MOI_SUAT;
int ve_da_ban[SO_QUAY] = {0};
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

void *ban_ve(void *arg) {
    int quay = *(int *)arg;

    while (1) {
        pthread_mutex_lock(&lock);

        if (tong_ve_con_lai == 0) {
            pthread_mutex_unlock(&lock);
            break;
        }

        int suat = rand() % SO_SUAT_CHIEU;
        if (ve_con_lai[suat] > 0) {
            ve_con_lai[suat]--;
            tong_ve_con_lai--;
            ve_da_ban[quay]++;
        }

        pthread_mutex_unlock(&lock);
    }

    return NULL;
}

int main(void) {
    pthread_t threads[SO_QUAY];
    int ma_quay[SO_QUAY];
    int so_thread = 0;

    for (int i = 0; i < SO_SUAT_CHIEU; i++) {
        ve_con_lai[i] = VE_MOI_SUAT;
    }

    for (int i = 0; i < SO_QUAY; i++) {
        ma_quay[i] = i;
        if (pthread_create(&threads[i], NULL, ban_ve, &ma_quay[i]) != 0) {
            fprintf(stderr, "Khong the tao thread cho quay %d\n", i + 1);
            break;
        }
        so_thread++;
    }

    for (int i = 0; i < so_thread; i++) {
        pthread_join(threads[i], NULL);
    }

    if (so_thread != SO_QUAY) {
        pthread_mutex_destroy(&lock);
        return 1;
    }

    for (int i = 0; i < SO_QUAY; i++) {
        printf("Quay %d da ban %d ve\n", i + 1, ve_da_ban[i]);
    }
    printf("Tong so ve con lai: %d\n", tong_ve_con_lai);

    pthread_mutex_destroy(&lock);
    return 0;
}
