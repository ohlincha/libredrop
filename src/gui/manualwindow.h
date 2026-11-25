#pragma once
// Help ▸ Manual: docs/MANUAL.md and its screenshots, from the program's resources (works offline).
#include <QDialog>

class QTextBrowser;

class ManualWindow : public QDialog {
    Q_OBJECT
public:
    explicit ManualWindow(QWidget* parent = nullptr);
};
