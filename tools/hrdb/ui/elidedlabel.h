#ifndef ELIDEDLABEL_H
#define ELIDEDLABEL_H

#include <QFrame>
#include <QString>

// This is a simplified version of the Qt example ElidedLabel, which
// supports a single line which is vertically aligned.
class ElidedLabel : public QFrame
{
    Q_OBJECT
    Q_PROPERTY(QString text READ text WRITE setText)

public:
    explicit ElidedLabel(const QString &text, QWidget *parent = nullptr);

    void setText(const QString &text);
    const QString & text() const { return content; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString content;
};

#endif // ELIDEDLABEL_H
