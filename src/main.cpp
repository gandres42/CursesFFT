#include <portaudio.h>
#include <sliding_dft.hpp>
#include <complex>
#include <iostream>
#include <ncurses.h>
#include <math.h>
#include <fftw3.h>
#include <chrono>

#define FFT_HEIGHT 20
#define Y_BUFFER_SIZE 23
#define Y_SIZE 24
#define X_SIZE 80

using namespace std;

typedef struct fft_wrapper
{
    int fft_size;
    int fft_out_size;
    int sample_rate;
    double *input;
    fftw_complex *output;
    double *amp_output;
    fftw_plan plan;
    PaStream *stream;
    char ** buffer;
    WINDOW * win;
    int graph_refresh_rate;
    uint64_t prev_refresh;
    int buffer_start;
} fft_wrapper_t;

typedef struct fft_settings
{
    int sample_rate;
    int fft_size;
    int refresh_rate;
} fft_settings_t;

int Freq2Index(double freq, int sample_rate, int fft_size)
{
    return (int)(freq / (sample_rate / fft_size));
}

double Index2Freq(int index, int sample_rate, int fft_size)
{
    return (sample_rate/(double)fft_size) * index;
}

uint64_t timeSinceEpochMillisec() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

int pa_fftw_callback(const void *inputBuffer, void *outputBuffer, unsigned long framesPerBuffer, const PaStreamCallbackTimeInfo *timeInfo, PaStreamCallbackFlags statusFlags, void *userData)
{
    float *input = (float *)inputBuffer;
    fft_wrapper_t * wrapper = ((fft_wrapper_t *)userData);

    for (int i = 0; i < framesPerBuffer; i++)
    {
        wrapper->input[i] = input[i];
    }

    fftw_execute(wrapper->plan);

    for (int i = 0; i < wrapper->fft_out_size; i++)
    {
        wrapper->amp_output[i] = sqrt(pow(wrapper->output[i][0], 2) + pow(wrapper->output[i][1], 2)) * (i/(double)(i + (wrapper->fft_size / 4)));
    }

    for (int x = 0; x < wrapper->fft_out_size; x++)
    {
        for (int y = 1; y < Y_BUFFER_SIZE; y++)
        {
            if (y < (int)(wrapper->amp_output[x] / .25))
            {
                wrapper->buffer[x][y] = 'X';
            }
            else
            {
                wrapper->buffer[x][y] = ' ';   
            }
        }
    }

    if (wrapper->graph_refresh_rate != 0 && timeSinceEpochMillisec() - wrapper->prev_refresh >= wrapper->graph_refresh_rate)
    {
        wclear(wrapper->win);

        for (int x = 0; x < min(X_SIZE, wrapper->fft_out_size); x++)
        {
            for (int y = 0; y < Y_BUFFER_SIZE; y++)
            {
                
                if (wrapper->buffer[x + wrapper->buffer_start][y] == 'X')
                {
                    wattron(wrapper->win, COLOR_PAIR(1));
                    mvwprintw(wrapper->win, Y_BUFFER_SIZE - 1 - y, x, " ");
                }
                else
                {
                    wattroff(wrapper->win, COLOR_PAIR(1));
                    mvwprintw(wrapper->win, Y_BUFFER_SIZE - 1 - y, x, "%c", wrapper->buffer[x + wrapper->buffer_start][y]);
                }
                
            }
        }
        wrefresh(wrapper->win);
        wrapper->prev_refresh = timeSinceEpochMillisec();
    }

    return 0;
}

void init_fft_wrapper(fft_wrapper * wrapper, int sample_rate, int fft_size, int refresh_rate)
{
    wrapper->fft_size = fft_size;
    wrapper->fft_out_size = (fft_size / 2);
    wrapper->sample_rate = sample_rate;
    wrapper->input = (double *)fftw_malloc(sizeof(double) * fft_size);
    wrapper->output = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * ((fft_size / 2) + 1));
    wrapper->amp_output = (double *)malloc(sizeof(double) * ((fft_size / 2 )+ 1));
    wrapper->plan = fftw_plan_dft_r2c_1d(fft_size, wrapper->input, wrapper->output, 0);
    wrapper->buffer = (char **)malloc(sizeof(char *) * wrapper->fft_out_size);

    for (int i = 0; i < wrapper->fft_out_size; i++)
    {
        wrapper->buffer[i] = (char *)malloc(sizeof(char *) * Y_SIZE);
    }

    for (int i = 0; i < wrapper->fft_out_size; i++)
    {
        for (int j = 0; j < Y_SIZE; j++)
        {
            wrapper->buffer[i][j] = ' ';
        }
    }
    for (int x = 0; x < wrapper->fft_out_size; x++)
    {
        wrapper->buffer[x][0] = '-';
    }
    for (int x = 0; x < wrapper->fft_out_size; x++)
    {
        if (x % 32 == 0)
        {
            string num = to_string((int)Index2Freq(x, wrapper->sample_rate, wrapper->fft_size));
            for (int i = 0; i < num.length(); i++)
            {
                wrapper->buffer[x + i][0] = num.at(i);
            }
        }
    }

    for (int i = 0; i < fft_size; i++)
    {
        wrapper->input[i] = 0;
    }

    wrapper->win = newwin(Y_BUFFER_SIZE, 80, 0, 0);
    wrapper->graph_refresh_rate = 1000 / refresh_rate;
    wrapper->prev_refresh = timeSinceEpochMillisec();
    wrapper->buffer_start = 0;

    Pa_OpenDefaultStream(&wrapper->stream,
                        1,                 /* mono input channel */
                        0,                 /* no output */
                        paFloat32,         /* 32 bit floating point output */
                        sample_rate,       /* sample rate */
                        fft_size,          /* frames per buffer, i.e. the number of sample frames that PortAudio will request from the callback. Many apps may want to use paFramesPerBufferUnspecified, which tells PortAudio to pick the best, possibly changing, buffer size.*/
                        pa_fftw_callback,  /* callback function */
                        wrapper);          /* pointer that will be passed to callback*/
}

void kill_fft_wrapper(fft_wrapper * wrapper)
{
    fftw_free(wrapper->input);
    fftw_free(wrapper->output);
    free(wrapper->amp_output);
    Pa_CloseStream(wrapper->stream);

    for (int i = 0; i < wrapper->fft_out_size; i++)
    {
        free(wrapper->buffer);
    }
    free(wrapper->buffer);

    free(wrapper);
}

void settings_menu(fft_wrapper * wrapper)
{
    int rig_refresh_rate = wrapper->graph_refresh_rate;
    wrapper->graph_refresh_rate = 0;
    WINDOW * win = newwin(7, 36, 9, 22);
    keypad(win, TRUE);

    int sizes[] = {64, 128, 256, 512, 1024, 2048};
    int sample_rates[] = {44100, 48000, 96000, 128000};
    int refresh_rates[] = {10, 30, 60, 120};

    int size_index = 0;
    for (int i = 0; i < 6; i++)
    {
        if (sizes[i] == wrapper->fft_size)
        {
            size_index = i;
            break;
        }
    }

    int sample_rate_index = 0;
    for (int i = 0; i < 4; i++)
    {
        if (sample_rates[i] == wrapper->sample_rate)
        {
            sample_rate_index = i;
            break;
        }
    }

    int refresh_rate_index = 0;
    for (int i = 0; i < 4; i++)
    {
        if (1000 / refresh_rates[i] == rig_refresh_rate)
        {
            refresh_rate_index = i;
            break;
        }
    }

    for (int x = 0; x < 36; x++)
    {
        for (int y = 0; y < 7; y++)
        {
            if (x == 0 || y == 0 || x == 35 || y == 6)
            {
                wattron(win, COLOR_PAIR(1));
                mvwprintw(win, y, x, " ");
            }
            else
            {
                wattroff(win, COLOR_PAIR(1));
                mvwprintw(win, y, x, " ");
            }
        }
    }

    wattroff(win, COLOR_PAIR(1));
    mvwprintw(win, 2, 4, "Input Buffer Size:\t< %i >", sizes[size_index]);
    mvwprintw(win, 3, 4, "Sample Rate:\t< %i >", sample_rates[sample_rate_index]);
    mvwprintw(win, 4, 4, "Graph Refresh Rate:\t< %i >", refresh_rates[refresh_rate_index]);

    wattron(win, COLOR_PAIR(1));
    mvwprintw(win, 0, 14, "SETTINGS");
    wrefresh(win);

    fft_settings_t settings;
    settings.fft_size = wrapper->fft_size;
    settings.refresh_rate = wrapper->graph_refresh_rate;
    settings.sample_rate = wrapper->sample_rate;



    wgetch(win);
    wrapper->graph_refresh_rate = rig_refresh_rate;
    endwin();
    return;
}

int main(int argc, char *argv[])
{
    initscr();
    start_color();
    curs_set(0);
    init_pair(1, COLOR_BLACK, COLOR_WHITE);
    Pa_Initialize();

    fft_wrapper_t *wrapper = (fft_wrapper_t *)malloc(sizeof(fft_wrapper_t));
    init_fft_wrapper(wrapper, 44100, 256, 60);
    Pa_StartStream(wrapper->stream);
    
    WINDOW * input_win = newwin(1, 80, Y_BUFFER_SIZE, 0);
    keypad(input_win, TRUE);
    mousemask(BUTTON1_CLICKED|BUTTON4_PRESSED|BUTTON2_PRESSED, NULL);
    wprintw(input_win, "s: settings, ->: move window right, <-: move window left");

    while (true)
    {
        int int_getch = wgetch(input_win);
        wclear(input_win);

        wrefresh(input_win);

        if (int_getch == 'q')
        {
            break;
        }
        else if (int_getch == 's')
        {
            settings_menu(wrapper);
        }
        else if (int_getch == KEY_RIGHT && wrapper->buffer_start < wrapper->fft_out_size - 80)
        {
            wrapper->buffer_start++;
        }
        else if (int_getch == KEY_LEFT && wrapper->buffer_start > 0)
        {
            wrapper->buffer_start--;
        }
        else if (int_getch == KEY_MOUSE)
        {
            MEVENT event;
            if(getmouse(&event) == OK)
			{
				if(event.bstate & BUTTON5_PRESSED && wrapper->buffer_start < wrapper->fft_out_size - 80)
                {
                    wrapper->buffer_start++;
				}
                else if(event.bstate & BUTTON4_PRESSED && wrapper->buffer_start > 0)
                {
                    wrapper->buffer_start--;
				}
			}
        }

        wclear(input_win);
        wprintw(input_win, "s: settings, ->: move window right, <-: move window left");
    }
    
    Pa_CloseStream(wrapper->stream);
    Pa_Terminate();

    endwin();
}