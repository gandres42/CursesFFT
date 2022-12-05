#include <portaudio.h>
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
    WINDOW * settings_win;
    int combined_bins;
} fft_wrapper_t;

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

    for (int i = 0; i < wrapper->fft_out_size / wrapper->combined_bins; i++)
    {
        double average = 0;
        for (int j = 0; j < wrapper->combined_bins; j++)
        {
            int x = (i * wrapper->combined_bins) + j;
            average += sqrt(pow(wrapper->output[x][0], 2) + pow(wrapper->output[x][1], 2)) * (x/(double)(x + (wrapper->fft_size / 2)));
        }
        average = average / wrapper->combined_bins;
        wrapper->amp_output[i] = average;
    }

    for (int x = 0; x < wrapper->fft_out_size / wrapper->combined_bins; x++)
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

        for (int x = 0; x < min(X_SIZE, wrapper->fft_out_size / wrapper->combined_bins); x++)
        {
            for (int y = 0; y < Y_BUFFER_SIZE; y++)
            {
                
                if (wrapper->buffer[x + wrapper->buffer_start][y] == 'X')
                {
                    if (wrapper->settings_win != nullptr)
                        wattron(wrapper->win, COLOR_PAIR(2));
                    else
                        wattron(wrapper->win, COLOR_PAIR(1));
                    mvwprintw(wrapper->win, Y_BUFFER_SIZE - 1 - y, x, " ");
                }
                else
                {
                    if (wrapper->settings_win != nullptr)
                        wattron(wrapper->win, COLOR_PAIR(3));
                    else
                        wattron(wrapper->win, COLOR_PAIR(4));
                    mvwprintw(wrapper->win, Y_BUFFER_SIZE - 1 - y, x, "%c", wrapper->buffer[x + wrapper->buffer_start][y]);
                }
                
            }
        }
        wrefresh(wrapper->win);
        wrapper->prev_refresh = timeSinceEpochMillisec();
    }

    if (wrapper->settings_win != nullptr)
    {
        redrawwin(wrapper->settings_win);
        wrefresh(wrapper->settings_win);   
    }

    return 0;
}

void init_fft_wrapper(fft_wrapper * wrapper, int sample_rate, int fft_size, int refresh_rate, int combined_bins)
{
    wrapper->fft_size = fft_size;
    wrapper->fft_out_size = (fft_size / 2) + 1;
    wrapper->sample_rate = sample_rate;
    wrapper->input = (double *)fftw_malloc(sizeof(double) * fft_size);
    wrapper->output = (fftw_complex *)fftw_malloc(sizeof(fftw_complex) * wrapper->fft_out_size);
    wrapper->plan = fftw_plan_dft_r2c_1d(fft_size, wrapper->input, wrapper->output, 0);
    wrapper->amp_output = (double *)malloc(sizeof(double) *  (wrapper->fft_out_size / combined_bins));
    wrapper->combined_bins = combined_bins;

    // initialize video buffer
    wrapper->buffer = (char **)malloc(sizeof(char *) * ( wrapper->fft_out_size) / combined_bins);
    for (int i = 0; i < wrapper->fft_out_size / combined_bins; i++)
    {
        wrapper->buffer[i] = (char *)malloc(sizeof(char *) * Y_SIZE);
    }
    for (int i = 0; i < wrapper->fft_out_size / combined_bins; i++)
    {
        for (int j = 0; j < Y_SIZE; j++)
        {
            wrapper->buffer[i][j] = ' ';
        }
    }
    for (int x = 0; x < wrapper->fft_out_size / combined_bins; x++)
    {
        wrapper->buffer[x][0] = '-';
    }
    for (int x = 0; x < wrapper->fft_out_size / combined_bins; x++)
    {
        if (x % 32 == 0)
        {
            string num = to_string((int)Index2Freq(x, wrapper->sample_rate, wrapper->fft_size));
            for (int i = 0; i < num.length(); i++)
            {
                if (x + i < wrapper->fft_out_size)
                    wrapper->buffer[x + i][0] = num.at(i);
            }
        }
    }

    // zero out input
    for (int i = 0; i < fft_size; i++)
    {
        wrapper->input[i] = 0;
    }

    wrapper->win = newwin(Y_BUFFER_SIZE, 80, 0, 0);
    wrapper->graph_refresh_rate = 1000 / refresh_rate;
    wrapper->prev_refresh = timeSinceEpochMillisec();
    wrapper->buffer_start = 0;
    wrapper->settings_win = nullptr;

    Pa_OpenDefaultStream(&wrapper->stream,
                        1,                 /* mono input channel */
                        0,                 /* no output */
                        paFloat32,         /* 32 bit floating point output */
                        sample_rate,       /* sample rate */
                        fft_size,          /* frames per buffer, i.e. the number of sample frames that PortAudio will request from the callback. Many apps may want to use paFramesPerBufferUnspecified, which tells PortAudio to pick the best, possibly changing, buffer size.*/
                        pa_fftw_callback,  /* callback function */
                        wrapper);          /* pointer that will be passed to callback*/
}

void update_fft_wrapper(fft_wrapper * wrapper,  int sample_rate, int fft_size, int refresh_rate)
{
    Pa_CloseStream(wrapper->stream);
    fftw_free(wrapper->input);
    fftw_free(wrapper->output);
    free(wrapper->amp_output);
    free(wrapper->buffer);

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
    wrapper->settings_win = nullptr;

    Pa_OpenDefaultStream(&wrapper->stream,
                        1,                 /* mono input channel */
                        0,                 /* no output */
                        paFloat32,         /* 32 bit floating point output */
                        sample_rate,       /* sample rate */
                        fft_size,          /* frames per buffer, i.e. the number of sample frames that PortAudio will request from the callback. Many apps may want to use paFramesPerBufferUnspecified, which tells PortAudio to pick the best, possibly changing, buffer size.*/
                        pa_fftw_callback,  /* callback function */
                        wrapper);          /* pointer that will be passed to callback*/   
    Pa_StartStream(wrapper->stream);
}

void kill_fft_wrapper(fft_wrapper * wrapper)
{
    Pa_CloseStream(wrapper->stream);
    fftw_free(wrapper->input);
    fftw_free(wrapper->output);
    free(wrapper->amp_output);
    free(wrapper->buffer);
    delwin(wrapper->win);
    free(wrapper);
}

void settings_menu(fft_wrapper * wrapper)
{
    int rig_refresh_rate = wrapper->graph_refresh_rate;
    wrapper->graph_refresh_rate = 100;

    WINDOW * win = newwin(9, 36, 7, 22);
    wrapper->settings_win = win;
    keypad(win, TRUE);

    int option_index = 0;

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

    // Draw settings window
    int inout = 0;

    while (inout != 's')
    {
        // frame
        for (int x = 0; x < 36; x++)
        {
            for (int y = 0; y < 9; y++)
            {
                if (x == 0 || y == 0 || x == 35 || y == 8)
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
        wattron(win, COLOR_PAIR(1));
        mvwprintw(win, 0, 14, "SETTINGS");
        
        // options
        // input buffer size
        wattroff(win, COLOR_PAIR(1));
        mvwprintw(win, 2, 4, "Input Buffer Size:\t");
        if (option_index == 0)
            wattron(win, COLOR_PAIR(1));
        else
            wattroff(win, COLOR_PAIR(1));
        wprintw(win, "< %i >", sizes[size_index]);
        
        // sample rate
        wattroff(win, COLOR_PAIR(1));
        mvwprintw(win, 3, 4, "Sample Rate:\t");
        if (option_index == 1)
            wattron(win, COLOR_PAIR(1));
        else
            wattroff(win, COLOR_PAIR(1));
        wprintw(win, "< %i >", sample_rates[sample_rate_index]);
        
        // refresh rate
        wattroff(win, COLOR_PAIR(1));
        mvwprintw(win, 4, 4, "Graph Refresh Rate:\t");
        if (option_index == 2)
            wattron(win, COLOR_PAIR(1));
        else
            wattroff(win, COLOR_PAIR(1));
        wprintw(win, "< %i >", refresh_rates[refresh_rate_index]);

        // cancel
        if (option_index == 3)
            wattron(win, COLOR_PAIR(1));
        else
            wattroff(win, COLOR_PAIR(1));
        mvwprintw(win, 6, 7, "Cancel");

        // apply settings
        if (option_index == 4)
            wattron(win, COLOR_PAIR(1));
        else
            wattroff(win, COLOR_PAIR(1));
        mvwprintw(win, 6, 23, "Apply");

        wrefresh(win);

        inout = wgetch(win);

        if (inout == KEY_DOWN && option_index < 4)
        {
            option_index++;
        }
        else if (inout == KEY_UP && option_index > 0)
        {
            option_index--;
        }
        else if (inout == KEY_RIGHT)
        {
            if (option_index == 0 && size_index < 5)
            {
                size_index++;
            }
            else if (option_index == 1 && sample_rate_index < 3)
            {
                sample_rate_index++;
            }
            else if (option_index == 2 && refresh_rate_index < 3)
            {
                refresh_rate_index++;
            }
        }
        else if (inout == KEY_LEFT)
        {
            if (option_index == 0 && size_index > 0)
            {
                size_index--;
            }
            else if (option_index == 1 && sample_rate_index > 0)
            {
                sample_rate_index--;
            }
            else if (option_index == 2 && refresh_rate_index > 0)
            {
                refresh_rate_index--;
            }
        }
        else if (inout == '\n' && option_index == 3)
        {
            inout = 's';
        }
        else if (inout == '\n' && option_index == 4)
        {
            update_fft_wrapper(wrapper, sample_rates[sample_rate_index], sizes[size_index], refresh_rates[refresh_rate_index]);
            return;
        }
    }

    wrapper->settings_win = nullptr;
    wrapper->graph_refresh_rate = rig_refresh_rate;
    delwin(win);
    return;
}

int main(int argc, char *argv[])
{
    initscr();
    start_color();
    curs_set(0);
    init_pair(1, COLOR_BLACK, COLOR_WHITE);
    init_pair(2, COLOR_BLACK, 0x8);
    init_pair(3, 0x8, COLOR_BLACK);
    init_pair(4, COLOR_WHITE, COLOR_BLACK);
    Pa_Initialize();

    fft_wrapper_t *wrapper = (fft_wrapper_t *)malloc(sizeof(fft_wrapper_t));
    init_fft_wrapper(wrapper, 44100, 256, 60, 1);
    Pa_StartStream(wrapper->stream);
    
    WINDOW * input_win = newwin(1, 80, Y_BUFFER_SIZE, 0);
    keypad(input_win, TRUE);
    mousemask(BUTTON4_PRESSED|BUTTON5_PRESSED, NULL);
    mouseinterval(0);
    wprintw(input_win, "s: settings, left/right arrows | scroll: move window");

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
        else if (int_getch == KEY_RIGHT && wrapper->buffer_start < (wrapper->fft_out_size / wrapper->combined_bins) - 80)
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
				if(event.bstate & BUTTON5_PRESSED && wrapper->buffer_start < (wrapper->fft_out_size / wrapper->combined_bins) - 80)
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
        wprintw(input_win, "s: settings, left/right arrows | scroll: move window");
    }

    kill_fft_wrapper(wrapper);
    Pa_Terminate();
    endwin();
}