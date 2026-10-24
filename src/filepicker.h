#pragma once

#include <QObject>
#include <QUrl>

// Open and save dialogs, behind an interface so tests can answer them.
class FilePicker : public QObject {
    Q_OBJECT

public:
    explicit FilePicker(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~FilePicker() = default;

    virtual void openImage() = 0;
    // format is png, svg, pdf, or csv.
    virtual void exportFile(const QUrl &suggestedUrl, const QString &format) = 0;

signals:
    void openSelected(const QUrl &url);
    void exportSelected(const QUrl &url);
    void failed(const QString &message);
};
