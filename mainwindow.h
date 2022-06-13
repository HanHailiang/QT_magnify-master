#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "stdafx.h"
#include <wincodec.h>
#include <magnification.h>
#include <threadpoolapiset.h>
#include <shellapi.h>
#include "MagWindow.h"
#include "resource.h"
#include"windows.h"
#include "WinUser.h"
#pragma comment  (lib, "User32.lib")
#pragma comment  (lib, "Kernel32.lib")
QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
protected:
    void timerEvent(QTimerEvent* event);
private:
    Ui::MainWindow *ui;
    quint16 time500ms;
};
#endif // MAINWINDOW_H
