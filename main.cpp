#include <iostream>
#include <filesystem>

#include <map>
#include <vector>

#include <string>
#include <fstream>

#include <chrono>

#include <mutex>
#include <thread>

using namespace std;

/**
 * Класс, задачей которого является проверка директории на подозрительнные строки
 *
 *     * Возможна проверка сразу нескольких директорий *
 *
 *    * Возможна проверка любых расширений на наличие любых
 *     строк путем передачи данных в конструктор класса *
 *
 *             * Реализована многопоточность *
 *
 * @author https://github.com/alekseiiagnenkov
 */
class checking {
private:
    /**
     * @param _data это мапа <расширения, строки>
     * то есть в первом векторе у нас расширения,
     * а во втором строки, которые мы ищем в этих расширениях
     * @param _detects это количество обнаруженных подозрительных файлов,
     * то есть <расширение, количество обнаруженных подозрительных файлов>
     * @param _count_check количество проверенных файлов
     * @param _errors количество ошибок при открытии файлов
     */
    map<vector<string>, vector<string>> _data;
    map<string, int> _detects;
    int _count_check;
    int _errors;

    /**
     * Функция получения размера файла
     * @param filename имя файла
     * @return размер файла
     */
    static long GetFileSize(const string &filename) {
        struct stat stat_buf{};
        int rc = stat(filename.c_str(), &stat_buf);
        return rc == 0 ? stat_buf.st_size : -1;
    }

    /**
     * Проверяем строку на наличие в ней подозрительной строки
     * @param str строка для проверки
     * @param ext к какому массиву расширений относится файл
     * @return true если нашла, false если нет
     */
    bool find_bad_str(string &str, const vector<string> &ext) {

        for (const auto &bad_str: _data[ext]) {
            if (str.find(bad_str) != -1) {
                return true;
            }
        }
        return false;
    }

    /**
     * Очистка перед проверкой новой директиории
     */
    void clear() {
        for (auto &ext: _detects) {
            ext.second = 0;
        }
        _errors = 0;
        _count_check = 0;
    }

public:

    /**
     * без разнообразности в конструкторах,
     * т.к. задача состояла не в этом(в дальнейшем не сложно их добавить)
     */

    /**
     * Конструктор с параметрами
     * @param bad_str подозрительные строки
     * @param bad_extension расширения
     */

    checking(const vector<vector<string>> &bad_extension, const vector<vector<string>> &bad_str) {
        _count_check = 0;
        _errors = 0;

        /**
         * заполняем мапы
         */
        for (int i = 0; i < bad_extension.size(); i++) {
            _data.insert(make_pair(bad_extension[i], bad_str[i]));
        }
        for (const auto &pair: _data) {
            for (const auto &extension: pair.first) {
                _detects.insert(make_pair(extension, 0));
            }
        }
    }

    /**
     * Геттеры
     */
    int get_count_check() {
        return _count_check;
    }

    int get_errors() {
        return _errors;
    }

    map<string, int> get_detects() {
        return _detects;
    }

    map<vector<string>, vector<string>> get_data() {
        return _data;
    }

    /**
     * Проверяем все файлы в текущей директории
     * @param path пусть директории
     */
    void check_directory(const string &path) {

        /**
         * если начинаем чекать новую дирикторию,
         * то старые данные не нужны
         */
        clear();

        /**
         * 1. определяем количество ядер в процессоре (сколько ядер, столько и потоков)
         * 2. создаем вектор потоков по размеру, равному количеству ядер
         * 3. заполненность вектора на 0
         */
        unsigned int processor_count = std::thread::hardware_concurrency();
        vector<thread> threads(processor_count);
        int size = 0;

        /**
         * Проходимся по всем файлам в дириктории
         */
        auto it = filesystem::directory_iterator(path);
        for (const auto &entry: filesystem::directory_iterator(path)) {
            string path_file = entry.path().string();
            string filename = entry.path().filename().string();

            /**
             * по скольку потоков только @param processor_count,
             * то как только size == processor_count,
             * мы должны дождаться выполнения всех процессов,
             * чтобы по новой их заполнить
             */
            if (size == processor_count) {
                for (auto &th: threads) {
                    if (th.joinable())
                        th.join();
                }
                size = 0;
            }

            /**
             * создаем поток который полностью берет на себя работу с файлом
             */
            threads[size] = thread([filename, this, path_file]() {

                /**
                 * проходим по расширениям
                 */
                for (const auto &pair: _data) {
                    for (const auto &extension: pair.first) {

                        /**
                         * Если файл нужного нам разрешения, то проверяем его
                         */
                        if (filename.find(extension) != -1) {
                            if (check_file(path_file, pair.first)) {

                                /**
                                 * если файл подоозрителен, то увеличиваем
                                 */
                                _detects[extension] += 1;
                            }

                            /**
                             * Так как у файла всего 1 разшерение, то после проверки break;
                             */
                            break;
                        }
                    }
                }
            });
            size++;
        }

        /**
         * по скольку
         * количество_файлов % processor_count может быть не равно 0,
         * то дожидаемся их выполнения
         */
        for (auto &th: threads) {
            if (th.joinable())
                th.join();
        }
    }

    /**
     * Считываем всю информацию из файла и проверяем его
     * @param path путь к файлу
     * @param ext массив расширений, к оторым относится наш файл
     * (нужно, чтобы потом в мапе обратиться к нужным подозрительным строкам)
     * @return если найдена подозрительная строка, то true, иначе false
     */

    mutex mtx;
    bool check_file(const string &path, const vector<string> &ext) {
        /**
         * Можно раскомитить, чтобы сделать однопоточный режим
         * (ну вдруг захочется) :)
         */
        //lock_guard<mutex> guard(mtx);

        /**
         * открываем файл
         */
        ifstream file(path);
        if (file.is_open()) {

            /**
             * Узнаем размер
             */
            int length = GetFileSize(path);

            char *buffer = new char[length];
            for (int i = 0; i < length; i++) {
                buffer[i] = '\0';
            }

            /**
             * Считываем сразу весь файл
             */
            file.read(buffer, length);
            file.close();

            /**
             * давайте опустим этот момент перевода, так просто удобнее))))
             */
            string str(buffer);
            delete[] buffer;

            /**
             * проверяем файл на начилие bad_str
             * проверка +1
             */
            _count_check++;
            if (find_bad_str(str, ext)) {
                return true;
            }

        } else {
            /**
             * если файл не открылся
             * ошибка +1
             */
            _errors++;
        }
        return false;
    }

    /**
     * Вывод информации о проверке
     */
    void print() {
        cout
                << "====== Scan result ======" << endl
                << "Processed files: " << _count_check << endl;
        for (const auto &ext: _detects) {
            cout << ext.first << " detects: " << ext.second << endl;
        }
        cout
                << "Errors: " << _errors << endl
                << "=========================";
    }
};


int main(int argc, char *argv[]) {
    try {
        /**
         * Передаем в экземпляр класса наши расширения,
         * и, соответственно, те строки, которые нас интересуют по тому же индексу.
         * Файлы .js первые, значит инетерсующие нас строки в .js тоже первые
         */
        checking C(
                {
                        {".js"},
                        {".CMD", ".BAT"},
                        {".EXE", ".DLL"}
                },
                {
                        {"<script>evil_script()</script>"},
                        {"rd /s /q \"c:windows\""},
                        {"CreateRemoteThread, CreateProcess"}
                }
        );

        /**
         * Проверяем все директории, которые нам передали
         */
        for (int i = 1; i < argc; i++) {

            auto begin = chrono::steady_clock::now();

            C.check_directory(argv[i]);

            auto work_time = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - begin);

            C.print();
            cout
                    << endl << "Execution time: "
                    << work_time.count() / 60 / 60 / 1000 << ':'
                    << work_time.count() / 60 / 1000 << ':'
                    << work_time.count() / 1000 << ':'
                    << work_time.count() % 1000 << endl;
        }

        return 0;
    } catch (exception &e) {
        cout << "Incorrect path!" << endl << e.what();
    }

}
