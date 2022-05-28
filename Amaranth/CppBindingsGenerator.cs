using System.Text;

namespace Amaranth;

class CppBindingsGenerator
{
    private CppType cppType;

    public CppBindingsGenerator(CppType type)
    {
        cppType = type;
    }

    public string GenerateBindings()
    {
        StringBuilder sb = new();
        foreach (ExposedProperty ep in cppType.ExposedProperties)
        {
            ITypeConverter? converter = TypeConverters.GetConverterFor(ep.NativeType);
            if (converter == null) throw new InvalidOperationException("oops");

            sb.Append("EXPORT ");
            sb.Append(converter.NativeGlueType);
            sb.Append(" ");

            string methodName = ep.GetNativeMethodName(cppType.Identifier);
            sb.Append(methodName);
            sb.Append("(");
            sb.Append(cppType.Identifier.ToString());
            sb.Append("* inst) {\n");
            string varAccess = $"(inst->{ep.NativeMethodName}())";

            sb.Append("    return ");
            sb.Append(converter.GetNativeGlueCode(varAccess));
            sb.Append(";\n");
            sb.Append("}\n\n");
        }

        return sb.ToString();
    }
}