import streamlit as st
import pandas as pd
import matplotlib.pyplot as plt

# leer datos
df = pd.read_csv("encuesta.csv")

st.title("Explorador de la encuesta")

# filtro por sexo
sexo = st.selectbox("Selecciona sexo", ["Todos"] + list(df["sexo"].unique()))

if sexo != "Todos":
    df = df[df["sexo"] == sexo]

st.write("Datos filtrados")
st.write(df)

# gráfico de colores
st.subheader("Distribución de colores")
conteo = df["color"].value_counts()

fig, ax = plt.subplots()
conteo.plot(kind="bar", ax=ax)
st.pyplot(fig)