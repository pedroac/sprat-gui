#pragma once
#include <functional>
#include <optional>
#include <QPixmap>
#include <QPoint>
#include <QWidget>

class QFormLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;

/**
 * @brief The Puppet workspace's right panel: the selected puppet's, part's or variant's
 *        properties. It only shows them and asks for changes; PuppetWorkspace applies them.
 *
 * Names are checked while typing with the validator the workspace gives; a name with a
 * problem is shown under the field and put back when editing ends.
 */
class PuppetPropertiesPanel : public QWidget {
    Q_OBJECT
public:
    /// Returns why a name can't be used, or an empty string when it can.
    using NameValidator = std::function<QString(const QString&)>;

    explicit PuppetPropertiesPanel(QWidget* parent = nullptr);

    void showNothing();
    void showPuppet(const QString& name, int partCount, NameValidator validator);
    /// @param position Set for a part placed by position; unset for an attached one
    /// @param attachment How an attached part hangs, e.g. "face, marker nose"
    /// @param problem Why the part can't be drawn, if it can't
    void showPart(const QString& name, std::optional<QPoint> position, const QString& attachment,
                  const QString& problem, NameValidator validator);
    /// @param explicitName The variant's own name, empty when it goes by @p defaultName
    /// @param source What it shows, e.g. "Sprite sprites/face.png"
    void showVariant(const QString& explicitName, const QString& defaultName, const QString& source,
                     const QPixmap& thumbnail, bool isRest, NameValidator validator);

signals:
    void renameRequested(const QString& name);
    void positionChanged(const QPoint& position);
    void makeRestVariantRequested();

private:
    QWidget* buildNameRow(QLineEdit*& edit, QLabel*& problem);
    void commitName(QLineEdit* edit, QLabel* problem);
    void checkName(QLineEdit* edit, QLabel* problem);

    QStackedWidget* m_pages = nullptr;
    NameValidator m_validator;
    QString m_shownName;  ///< The name in the field when it was shown

    QLineEdit* m_puppetName = nullptr;
    QLabel* m_puppetNameProblem = nullptr;
    QLabel* m_puppetInfo = nullptr;

    QLineEdit* m_partName = nullptr;
    QLabel* m_partNameProblem = nullptr;
    QFormLayout* m_partForm = nullptr;
    QWidget* m_positionRow = nullptr;
    QSpinBox* m_positionX = nullptr;
    QSpinBox* m_positionY = nullptr;
    QLabel* m_attachment = nullptr;
    QLabel* m_partProblem = nullptr;

    QLineEdit* m_variantName = nullptr;
    QLabel* m_variantNameProblem = nullptr;
    QLabel* m_variantSource = nullptr;
    QLabel* m_variantThumbnail = nullptr;
    QLabel* m_restLabel = nullptr;
    QPushButton* m_makeRestButton = nullptr;
};
