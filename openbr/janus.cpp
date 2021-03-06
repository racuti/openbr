#ifdef BR_LIBRARY
  #define JANUS_LIBRARY
#endif

#include "janus.h"
#include "openbr_plugin.h"

// Use the provided default implementation of some functions
#include "janus/src/janus.cpp"

using namespace br;

static QSharedPointer<Transform> transform;
static QSharedPointer<Distance> distance;

janus_error janus_initialize(const char *sdk_path, const char *model_file)
{
    int argc = 1;
    const char *argv[1] = { "janus" };
    Context::initialize(argc, (char**)argv, sdk_path);
    QString algorithm = model_file;
    if (algorithm.isEmpty()) algorithm = "Cvt(Gray)+Affine(88,88,0.25,0.35)+<FaceRecognitionExtraction>+<FaceRecognitionEmbedding>+<FaceRecognitionQuantization>:ByteL1";
    transform = Transform::fromAlgorithm(algorithm, false);
    distance = Distance::fromAlgorithm(algorithm);
    return JANUS_SUCCESS;
}

janus_error janus_finalize()
{
    Context::finalize();
    return JANUS_SUCCESS;
}

struct janus_template_type : public Template
{};

janus_error janus_initialize_template(janus_template *template_)
{
    *template_ = new janus_template_type();
    return JANUS_SUCCESS;
}

janus_error janus_add_image(const janus_image image, const janus_attribute_list attributes, janus_template template_)
{
    Template t;
    t.append(cv::Mat(image.height,
                     image.width,
                     image.color_space == JANUS_GRAY8 ? CV_8UC1 : CV_8UC1,
                     image.data));
    for (size_t i=0; i<attributes.size; i++)
        t.file.set(janus_attribute_to_string(attributes.attributes[i]), attributes.values[i]);

    if (!t.file.contains("JANUS_RIGHT_EYE_X") ||
        !t.file.contains("JANUS_RIGHT_EYE_Y") ||
        !t.file.contains("JANUS_LEFT_EYE_X") ||
        !t.file.contains("JANUS_LEFT_EYE_Y"))
        return JANUS_SUCCESS;

    t.file.set("Affine_0", QPointF(t.file.get<float>("JANUS_RIGHT_EYE_X"), t.file.get<float>("JANUS_RIGHT_EYE_Y")));
    t.file.set("Affine_1", QPointF(t.file.get<float>("JANUS_LEFT_EYE_X"), t.file.get<float>("JANUS_LEFT_EYE_Y")));
    Template u;
    transform->project(t, u);
    template_->append(u);
    return JANUS_SUCCESS;
}

janus_error janus_finalize_template(janus_template template_, janus_flat_template flat_template, size_t *bytes)
{    
    size_t templateBytes = 0;
    size_t numTemplates = 0;
    *bytes = sizeof(templateBytes) + sizeof(numTemplates);
    janus_flat_template pos = flat_template + *bytes;

    foreach (const cv::Mat &m, *template_) {
        assert(m.isContinuous());
        const size_t currentTemplateBytes = m.rows * m.cols * m.elemSize();
        if (templateBytes == 0)
            templateBytes = currentTemplateBytes;
        if (templateBytes != currentTemplateBytes)
            return JANUS_UNKNOWN_ERROR;
        if (*bytes + templateBytes > janus_max_template_size())
            break;
        memcpy(pos, m.data, templateBytes);
        *bytes += templateBytes;
        pos = pos + templateBytes;
        numTemplates++;
    }

    *(reinterpret_cast<size_t*>(flat_template)+0) = templateBytes;
    *(reinterpret_cast<size_t*>(flat_template)+1) = numTemplates;
    delete template_;
    return JANUS_SUCCESS;
}

janus_error janus_verify(const janus_flat_template a, const size_t a_bytes, const janus_flat_template b, const size_t b_bytes, double *similarity)
{
    (void) a_bytes;
    (void) b_bytes;

    size_t a_template_bytes, a_templates, b_template_bytes, b_templates;
    a_template_bytes = *(reinterpret_cast<size_t*>(a)+0);
    a_templates = *(reinterpret_cast<size_t*>(a)+1);
    b_template_bytes = *(reinterpret_cast<size_t*>(b)+0);
    b_templates = *(reinterpret_cast<size_t*>(b)+1);
    if (a_template_bytes != b_template_bytes)
        return JANUS_UNKNOWN_ERROR;

    float dist = 0;
    for (size_t i=0; i<a_templates; i++)
        for (size_t j=0; j<b_templates; j++)
            dist += distance->compare(cv::Mat(1, a_template_bytes, CV_8UC1, a+2*sizeof(size_t)+i*a_template_bytes),
                                      cv::Mat(1, b_template_bytes, CV_8UC1, b+2*sizeof(size_t)+i*b_template_bytes));
    *similarity = a_templates * b_templates / dist;
    return JANUS_SUCCESS;
}

struct janus_gallery_type : public TemplateList
{};

janus_error janus_initialize_gallery(janus_gallery *gallery)
{
    *gallery = new janus_gallery_type();
    return JANUS_SUCCESS;
}

janus_error janus_enroll(const janus_template template_, const janus_template_id template_id, janus_gallery gallery)
{
    template_->file.set("Template_ID", template_id);
    gallery->append(*template_);
    delete template_;
    return JANUS_SUCCESS;
}

janus_error janus_finalize_gallery(janus_gallery gallery, janus_gallery_file gallery_file)
{
    QFile file(gallery_file);
    if (!file.open(QFile::WriteOnly))
        return JANUS_WRITE_ERROR;
    QDataStream stream(&file);
    stream << gallery;
    file.close();
    return JANUS_SUCCESS;
}
